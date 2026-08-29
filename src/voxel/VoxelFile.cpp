//
// Created by Andrei Ghita on 26.08.2026.
//

#include "voxel/VoxelFile.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <raylib.h>

#include "voxel/SingleChunkGrid.hpp"
#include "voxel/VoxelMap.hpp"

namespace {

constexpr uint8_t ENCODING_RAW = 0;
constexpr uint8_t ENCODING_RLE = 1;
constexpr size_t CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
// A run length is written as a u16, so no run may be longer than this. A chunk
// only holds CHUNK_VOLUME voxels today, so this never bites, but the encoder
// must not start emitting unreadable runs if CHUNK_SIZE ever grows.
constexpr uint16_t MAX_RUN = 0xFFFF;
// Not a real limit on a scene, it is here so a corrupt grid_count cannot send
// the loader allocating without end.
constexpr size_t MAX_GRIDS = 4096;

/** The loaders load_grid() dispatches to, keyed by the grid type in a header. */
std::map<std::string, voxel_file::GridLoader>& registry() {
    static std::map<std::string, voxel_file::GridLoader> loaders = {
        {VOXEL_MAP_STR, &VoxelMap::load_body},
        {SINGLE_CHUNK_GRID_STR, &SingleChunkGrid::load_body},
    };
    return loaders;
}

// ------------------------------------------------------------ readable header

/** Makes a header value safe to put on one line. */
std::string escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string unescape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 == value.size()) {
            out += value[i];
            continue;
        }
        switch (value[++i]) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case '\\': out += '\\'; break;
            // An escape we do not know is kept as it was written
            default: out += '\\'; out += value[i]; break;
        }
    }
    return out;
}

std::string trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

/**
 * One header line. The file is opened in binary mode so that the offset the
 * binary section starts at is exact, which leaves any '\r' of a CRLF file for
 * this to strip.
 */
bool read_line(std::istream& in, std::string* out) {
    if (!std::getline(in, *out)) return false;
    if (!out->empty() && out->back() == '\r') out->pop_back();
    return true;
}

/** "1 2 3" into the ids it lists. Anything unreadable is skipped. */
std::vector<uint32_t> parse_id_list(const std::string& value) {
    std::vector<uint32_t> ids;
    std::istringstream stream(value);
    std::string token;
    // Commas are allowed so a hand written list can use either separator
    while (stream >> token) {
        token.erase(std::remove(token.begin(), token.end(), ','), token.end());
        if (token.empty()) continue;
        try {
            ids.emplace_back(static_cast<uint32_t>(std::stoul(token)));
        } catch (const std::exception&) {
            TraceLog(LOG_WARNING, "VOXELFILE: '%s' is not an id, skipped", token.c_str());
        }
    }
    return ids;
}

std::string format_id_list(const std::vector<uint32_t>& ids) {
    std::ostringstream out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i != 0) out << ' ';
        out << ids[i];
    }
    return out.str();
}

/** "4 5 6" into the grid coordinate it names. */
bool parse_int3(const std::string& value, Int3* out) {
    // Commas are allowed here too, so a hand written line can use either
    std::string cleaned = value;
    std::replace(cleaned.begin(), cleaned.end(), ',', ' ');

    std::istringstream stream(cleaned);
    int x = 0, y = 0, z = 0;
    if (!(stream >> x >> y >> z)) return false;
    *out = Int3{x, y, z};
    return true;
}

std::string format_int3(const Int3& v) {
    std::ostringstream out;
    out << v.x << ' ' << v.y << ' ' << v.z;
    return out.str();
}

/** Splits a "key: value" line. Returns false for a line that is not one. */
bool parse_field(const std::string& line, std::string* key, std::string* value) {
    const size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    *key = trim(line.substr(0, colon));
    *value = unescape(trim(line.substr(colon + 1)));
    return true;
}

/** Fills in a grid header's named members from the lines it collected. */
void resolve_grid_header(voxel_file::GridHeader* grid) {
    if (const std::string* v = grid->find("type")) grid->grid_type = *v;
    if (const std::string* v = grid->find("name")) grid->name = *v;
    if (const std::string* v = grid->find("description")) grid->description = *v;

    // "none", an empty value or no line at all all mean a root
    if (const std::string* v = grid->find("parent"); v != nullptr && !v->empty() && *v != "none") {
        const std::vector<uint32_t> ids = parse_id_list(*v);
        if (!ids.empty()) {
            grid->has_parent = true;
            grid->parent_id = ids.front();
        }
    }
    if (const std::string* v = grid->find("children")) grid->children = parse_id_list(*v);
    if (const std::string* v = grid->find("palette_source")) grid->palette_from_parent = (*v == "parent");

    // An attachment needs both of its connectors to mean anything, so half of
    // one is dropped rather than guessed at
    const std::string* anchor = grid->find("anchor_voxel");
    const std::string* connector = grid->find("connector_voxel");
    if (anchor != nullptr || connector != nullptr) {
        Int3 anchor_voxel{}, connector_voxel{};
        if (anchor != nullptr && connector != nullptr &&
            parse_int3(*anchor, &anchor_voxel) && parse_int3(*connector, &connector_voxel)) {
            grid->has_attachment = true;
            grid->anchor_voxel = anchor_voxel;
            grid->connector_voxel = connector_voxel;
        } else {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u carries only half an attachment, ignoring it",
                     grid->id);
        }
    }
}

/**
 * Reads the whole readable part of an open stream, leaving it on the first byte
 * of the binary section.
 */
bool parse_header(std::istream& in, voxel_file::Header* out) {
    std::string line;
    if (!read_line(in, &line)) {
        TraceLog(LOG_WARNING, "VOXELFILE: file is empty");
        return false;
    }

    std::istringstream first(line);
    std::string magic;
    int version = 0;
    if (!(first >> magic >> version) || magic != voxel_file::MAGIC) {
        TraceLog(LOG_WARNING, "VOXELFILE: not a voxel grid file (bad magic)");
        return false;
    }
    if (version != voxel_file::FORMAT_VERSION) {
        TraceLog(LOG_WARNING, "VOXELFILE: file is version %i, this build reads version %i",
                 version, voxel_file::FORMAT_VERSION);
        return false;
    }
    out->version = version;
    out->fields.clear();
    out->grids.clear();

    // Lines land in the file's own fields until the first grid block opens, and
    // in that block's fields from then on
    voxel_file::GridHeader* current = nullptr;
    bool found_marker = false;

    while (read_line(in, &line)) {
        if (line == voxel_file::BINARY_MARKER) {
            found_marker = true;
            break;
        }
        if (line == voxel_file::GRID_MARKER) {
            if (out->grids.size() >= MAX_GRIDS) {
                TraceLog(LOG_WARNING, "VOXELFILE: more than %zu grids in one file", MAX_GRIDS);
                return false;
            }
            out->grids.emplace_back();
            current = &out->grids.back();
            // The id defaults to the block's position, so a hand written file
            // that leaves the ids out still has usable ones
            current->id = static_cast<uint32_t>(out->grids.size() - 1);
            continue;
        }
        if (line.empty() || line[0] == '#') continue;

        std::string key, value;
        if (!parse_field(line, &key, &value)) {
            TraceLog(LOG_WARNING, "VOXELFILE: header line without a ':' ignored: %s", line.c_str());
            continue;
        }

        if (current == nullptr) {
            out->fields[key] = value;
        } else {
            current->fields[key] = value;
            if (key == "id") {
                try {
                    current->id = static_cast<uint32_t>(std::stoul(value));
                } catch (const std::exception&) {
                    TraceLog(LOG_WARNING, "VOXELFILE: '%s' is not an id", value.c_str());
                }
            }
        }
    }

    if (!found_marker) {
        TraceLog(LOG_WARNING, "VOXELFILE: header never ended, no '%s' line", voxel_file::BINARY_MARKER);
        return false;
    }

    for (voxel_file::GridHeader& grid : out->grids) {
        resolve_grid_header(&grid);
        if (grid.grid_type.empty()) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u has no type", grid.id);
            return false;
        }
    }

    if (out->grids.empty()) {
        TraceLog(LOG_WARNING, "VOXELFILE: file holds no grids");
        return false;
    }
    // Informational, the blocks themselves are what is read
    const int claimed = out->get_int("grid_count", static_cast<int>(out->grids.size()));
    if (claimed != static_cast<int>(out->grids.size())) {
        TraceLog(LOG_WARNING, "VOXELFILE: header says %i grids, found %zu",
                 claimed, out->grids.size());
    }

    if (const voxel_file::GridHeader* root = out->root()) {
        out->name = root->name;
        out->description = root->description;
        out->grid_type = root->grid_type;
    }
    return true;
}

// ------------------------------------------------------------- binary section

void write_palette(std::ostream& out, const VoxelColourMap& colours) {
    const auto* palette = colours.get();
    const uint32_t palette_size = palette ? static_cast<uint32_t>(palette->size()) : 0;

    voxel_file::write_u32(out, palette_size);
    if (palette == nullptr) return;
    for (const auto& [id, colour] : *palette) {
        voxel_file::write_u8(out, id);
        voxel_file::write_u8(out, colour.r);
        voxel_file::write_u8(out, colour.g);
        voxel_file::write_u8(out, colour.b);
        voxel_file::write_u8(out, colour.a);
    }
}

bool read_palette(std::istream& in, VoxelColourMap* out) {
    uint32_t palette_size = 0;
    if (!voxel_file::read_u32(in, &palette_size)) return false;
    // Ids are one byte, so a palette can hold at most 256 distinct entries
    if (palette_size > 256) {
        TraceLog(LOG_WARNING, "VOXELFILE: palette of %u entries is too large", palette_size);
        return false;
    }

    auto palette = std::make_shared<std::map<VoxelID, Color>>();
    for (uint32_t i = 0; i < palette_size; ++i) {
        uint8_t id = 0, r = 0, g = 0, b = 0, a = 0;
        if (!voxel_file::read_u8(in, &id) || !voxel_file::read_u8(in, &r) ||
            !voxel_file::read_u8(in, &g) || !voxel_file::read_u8(in, &b) ||
            !voxel_file::read_u8(in, &a))
            return false;
        (*palette)[id] = Color{r, g, b, a};
    }
    *out = palette;
    return true;
}

/**
 * The local transform, not the world one: a grid saved while hanging off a
 * parent is stored where it sits inside that parent, so loading the tree back
 * puts every child in the same place again.
 */
void write_transform(std::ostream& out, const Transform& t) {
    voxel_file::write_f32(out, t.translation.x);
    voxel_file::write_f32(out, t.translation.y);
    voxel_file::write_f32(out, t.translation.z);
    voxel_file::write_f32(out, t.rotation.x);
    voxel_file::write_f32(out, t.rotation.y);
    voxel_file::write_f32(out, t.rotation.z);
    voxel_file::write_f32(out, t.rotation.w);
    voxel_file::write_f32(out, t.scale.x);
    voxel_file::write_f32(out, t.scale.y);
    voxel_file::write_f32(out, t.scale.z);
}

bool read_transform(std::istream& in, Transform* out) {
    float v[10] = {};
    for (float& f : v) {
        if (!voxel_file::read_f32(in, &f)) return false;
    }
    *out = Transform{
        Vector3{v[0], v[1], v[2]},
        Quaternion{v[3], v[4], v[5], v[6]},
        Vector3{v[7], v[8], v[9]},
    };
    return true;
}

/**
 * Where a body sits in the file, from the pass that indexes them. Offsets are
 * kept as streamoff rather than streampos, as only the former can be compared.
 */
struct BodyBlock {
    std::streamoff offset;
    uint32_t length;
};

/**
 * Walks the bodies without reading any of them, so that each one can afterwards
 * be found by the id it names rather than by its position.
 *
 * @param order: the ids in the order they were met, for the check against the
 *        order the headers are in.
 */
bool index_bodies(std::istream& in, const std::streamoff binary_start,
                  std::map<uint32_t, BodyBlock>* blocks, std::vector<uint32_t>* order) {
    in.clear();
    in.seekg(0, std::ios::end);
    const std::streamoff file_size = in.tellg();
    in.seekg(binary_start);
    if (!in) {
        TraceLog(LOG_WARNING, "VOXELFILE: could not reach the binary section");
        return false;
    }

    // Every turn of this consumes the eight bytes in front of a body at least,
    // so the walk always moves forward
    while (in.tellg() < file_size) {
        uint32_t id = 0, length = 0;
        if (!voxel_file::read_u32(in, &id) || !voxel_file::read_u32(in, &length)) {
            TraceLog(LOG_WARNING, "VOXELFILE: file ends inside a body header");
            return false;
        }

        const std::streamoff offset = in.tellg();
        // A length longer than what is left would send the walk past the end
        if (offset + static_cast<std::streamoff>(length) > file_size) {
            TraceLog(LOG_WARNING, "VOXELFILE: body %u claims %u bytes, only %lld are left",
                     id, length, static_cast<long long>(file_size - offset));
            return false;
        }

        if (!blocks->emplace(id, BodyBlock{offset, length}).second) {
            TraceLog(LOG_WARNING, "VOXELFILE: two bodies share id %u, the later one is ignored", id);
        } else {
            order->emplace_back(id);
        }

        in.clear();
        in.seekg(offset + static_cast<std::streamoff>(length));
        if (!in) {
            TraceLog(LOG_WARNING, "VOXELFILE: could not skip past body %u", id);
            return false;
        }
    }
    return true;
}

/**
 * Frees every grid a failed load built. Each one is deleted on its own, linked
 * or not: the hierarchy links do not own anything, and a destructor only
 * detaches, so nothing here is freed twice.
 */
void delete_loose_grids(const std::map<uint32_t, VoxelGrid*>& grids) {
    for (const auto& [id, grid] : grids) {
        delete grid;
    }
}

}

// ---------------------------------------------------------------- header keys

const std::string* voxel_file::GridHeader::find(const std::string& key) const {
    const auto it = fields.find(key);
    return it == fields.end() ? nullptr : &it->second;
}

int voxel_file::GridHeader::get_int(const std::string& key, const int fallback) const {
    const std::string* value = find(key);
    if (value == nullptr) return fallback;
    try {
        return std::stoi(*value);
    } catch (const std::exception&) {
        return fallback;
    }
}

const std::string* voxel_file::Header::find(const std::string& key) const {
    const auto it = fields.find(key);
    return it == fields.end() ? nullptr : &it->second;
}

int voxel_file::Header::get_int(const std::string& key, const int fallback) const {
    const std::string* value = find(key);
    if (value == nullptr) return fallback;
    try {
        return std::stoi(*value);
    } catch (const std::exception&) {
        return fallback;
    }
}

const voxel_file::GridHeader* voxel_file::Header::root() const {
    for (const GridHeader& grid : grids) {
        if (!grid.has_parent) return &grid;
    }
    return nullptr;
}

const voxel_file::GridHeader* voxel_file::Header::find_grid(const uint32_t id) const {
    for (const GridHeader& grid : grids) {
        if (grid.id == id) return &grid;
    }
    return nullptr;
}

// --------------------------------------------------------------------- saving

bool voxel_file::register_grid_loader(const std::string& grid_type, const GridLoader loader) {
    if (loader == nullptr) return false;
    return registry().insert({grid_type, loader}).second;
}

bool voxel_file::can_load_grid_type(const std::string& grid_type) {
    return registry().find(grid_type) != registry().end();
}

bool voxel_file::save_grid(VoxelGrid* grid, const std::string& path,
                           const std::string& name, const std::string& description) {
    if (grid == nullptr) {
        TraceLog(LOG_WARNING, "VOXELFILE: cannot save a null grid");
        return false;
    }
    grid->name = name;
    grid->description = description;
    return save_grid(grid, path);
}

bool voxel_file::save_grid(VoxelGrid* grid, const std::string& path) {
    if (grid == nullptr) {
        TraceLog(LOG_WARNING, "VOXELFILE: cannot save a null grid");
        return false;
    }

    // Parents before children, which is the order the ids are handed out in
    std::vector<VoxelGrid*> grids;
    collect_grids(grid, &grids);
    if (grids.size() > MAX_GRIDS) {
        TraceLog(LOG_WARNING, "VOXELFILE: %zu grids is more than a file may hold", grids.size());
        return false;
    }

    // An id is a position in that list, so the tree can be described by number
    std::map<const VoxelGrid*, uint32_t> ids;
    for (size_t i = 0; i < grids.size(); ++i) ids[grids[i]] = static_cast<uint32_t>(i);

    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            TraceLog(LOG_WARNING, "VOXELFILE: could not create '%s': %s",
                     parent.string().c_str(), ec.message().c_str());
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        TraceLog(LOG_WARNING, "VOXELFILE: could not open '%s' for writing", path.c_str());
        return false;
    }

    // '\n' is written explicitly, as the stream is in binary mode and must not
    // translate line endings
    out << MAGIC << ' ' << FORMAT_VERSION << '\n';
    out << "grid_count: " << grids.size() << '\n';
    out << "voxel_bytes: " << sizeof(VoxelID) << '\n';
    out << "chunk_size: " << CHUNK_SIZE << '\n';

    // Every header first, then every body, in the same order
    for (VoxelGrid* g : grids) {
        const VoxelGrid* g_parent = g->get_parent();
        // A grid meshed with its parent's colour map is written as sharing it,
        // so the two come back pointing at one map rather than two equal ones
        const bool shares = g_parent != nullptr && g->voxel_colours == g_parent->voxel_colours;

        std::vector<uint32_t> child_ids;
        for (const VoxelGrid* child : g->get_children()) {
            const auto it = ids.find(child);
            if (it != ids.end()) child_ids.emplace_back(it->second);
        }

        out << '\n' << GRID_MARKER << '\n';
        out << "id: " << ids[g] << '\n';
        out << "type: " << g->get_grid_type() << '\n';
        out << "name: " << escape(g->name) << '\n';
        out << "description: " << escape(g->description) << '\n';
        out << "parent: ";
        if (g_parent != nullptr && ids.count(g_parent)) out << ids[g_parent] << '\n';
        else out << "none" << '\n';
        out << "children: " << format_id_list(child_ids) << '\n';

        // An attachment is the parent link, which is already written above,
        // plus the voxel on each side that holds the two together
        if (const Attachment* attachment = g->get_attachment();
            attachment != nullptr && attachment->anchor == g_parent) {
            out << "anchor_voxel: " << format_int3(attachment->anchor_voxel) << '\n';
            out << "connector_voxel: " << format_int3(attachment->local_voxel) << '\n';
        }

        out << "palette_source: " << (shares ? "parent" : "own") << '\n';
        out << "palette_size: " << (shares || !g->voxel_colours ? 0 : g->voxel_colours->size()) << '\n';
    }

    out << '\n' << BINARY_MARKER << '\n';

    for (VoxelGrid* g : grids) {
        const VoxelGrid* g_parent = g->get_parent();
        const bool shares = g_parent != nullptr && g->voxel_colours == g_parent->voxel_colours;

        // Built away from the file first, as the length goes in front of it
        std::ostringstream payload;
        if (!shares) write_palette(payload, g->voxel_colours);
        write_transform(payload, g->get_transform());
        if (!g->write_body(payload)) {
            TraceLog(LOG_WARNING, "VOXELFILE: %s failed to write its body to '%s'",
                     g->get_grid_type().c_str(), path.c_str());
            return false;
        }

        const std::string bytes = payload.str();
        write_u32(out, ids[g]);
        write_u32(out, static_cast<uint32_t>(bytes.size()));
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    out.flush();
    if (!out.good()) {
        TraceLog(LOG_WARNING, "VOXELFILE: writing '%s' failed", path.c_str());
        return false;
    }
    TraceLog(LOG_INFO, "VOXELFILE: saved %s and %zu grid(s) below it to '%s'",
             grid->get_grid_type().c_str(), grids.size() - 1, path.c_str());
    return true;
}

// -------------------------------------------------------------------- loading

bool voxel_file::read_header(const std::string& path, Header* out) {
    if (out == nullptr) return false;

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        TraceLog(LOG_WARNING, "VOXELFILE: could not open '%s' for reading", path.c_str());
        return false;
    }
    return parse_header(in, out);
}

VoxelGrid* voxel_file::load_grid(const std::string& path, VoxelView* view,
                                 VoxelColourMap shared_palette, Header* out_header) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        TraceLog(LOG_WARNING, "VOXELFILE: could not open '%s' for reading", path.c_str());
        return nullptr;
    }

    Header header;
    if (!parse_header(in, &header)) return nullptr;
    const std::streamoff binary_start = in.tellg();

    // The voxels are the one part of the format that never changes shape, so a
    // file that disagrees about them cannot be read by any grid here
    if (header.get_int("voxel_bytes", static_cast<int>(sizeof(VoxelID))) != static_cast<int>(sizeof(VoxelID))) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' uses %i byte voxels, this build uses %i",
                 path.c_str(), header.get_int("voxel_bytes", 0), static_cast<int>(sizeof(VoxelID)));
        return nullptr;
    }
    if (header.get_int("chunk_size", CHUNK_SIZE) != CHUNK_SIZE) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' uses %i sized chunks, this build uses %i",
                 path.c_str(), header.get_int("chunk_size", 0), CHUNK_SIZE);
        return nullptr;
    }

    std::map<uint32_t, BodyBlock> blocks;
    std::vector<uint32_t> body_order;
    if (!index_bodies(in, binary_start, &blocks, &body_order)) {
        TraceLog(LOG_WARNING, "VOXELFILE: could not walk the bodies of '%s'", path.c_str());
        return nullptr;
    }

    // Bodies are meant to sit in the same order as the headers. Reading is by
    // id either way, so this only has to be said out loud, not fixed.
    const bool ordered = body_order.size() == header.grids.size()
        && std::equal(body_order.begin(), body_order.end(), header.grids.begin(),
                      [](const uint32_t id, const GridHeader& grid) { return id == grid.id; });
    if (!ordered) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' has %zu bodies for %zu headers, or not in header "
                              "order; reading them by id", path.c_str(),
                 body_order.size(), header.grids.size());
    }

    // Every grid is built first and hung off its parent afterwards, so that a
    // parent that comes later in the file is no trouble
    std::map<uint32_t, VoxelGrid*> built;

    for (const GridHeader& grid_header : header.grids) {
        if (built.count(grid_header.id)) {
            TraceLog(LOG_WARNING, "VOXELFILE: two headers share id %u", grid_header.id);
            delete_loose_grids(built);
            return nullptr;
        }

        const auto block = blocks.find(grid_header.id);
        if (block == blocks.end()) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u ('%s') has a header but no body",
                     grid_header.id, grid_header.name.c_str());
            delete_loose_grids(built);
            return nullptr;
        }

        const auto loader = registry().find(grid_header.grid_type);
        if (loader == registry().end()) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u holds an unknown grid type '%s'",
                     grid_header.id, grid_header.grid_type.c_str());
            delete_loose_grids(built);
            return nullptr;
        }

        in.clear();
        in.seekg(block->second.offset);

        LoadContext ctx{};
        ctx.view = view;
        ctx.file = &header;
        ctx.header = &grid_header;
        ctx.transform = identity();

        bool ok = true;
        if (grid_header.palette_from_parent) {
            // The parent is built by now: it is written before its children, and
            // a grid whose parent is missing simply keeps no palette of its own
            const auto parent = built.find(grid_header.parent_id);
            if (parent != built.end()) {
                ctx.palette = parent->second->voxel_colours;
            } else {
                TraceLog(LOG_WARNING, "VOXELFILE: grid %u shares the palette of %u, which is not "
                                      "built yet, so its colours may be wrong",
                         grid_header.id, grid_header.parent_id);
            }
        } else {
            ok = read_palette(in, &ctx.palette);
        }
        if (ok) ok = read_transform(in, &ctx.transform);
        // The caller may want everything on the same colours as the rest of the
        // scene, in which case the palettes in the file are dropped
        if (shared_palette) ctx.palette = shared_palette;

        VoxelGrid* built_grid = ok ? loader->second(in, ctx) : nullptr;
        if (built_grid == nullptr) {
            TraceLog(LOG_WARNING, "VOXELFILE: could not read the %s body of grid %u in '%s'",
                     grid_header.grid_type.c_str(), grid_header.id, path.c_str());
            delete_loose_grids(built);
            return nullptr;
        }

        // A loader that read the wrong number of bytes would otherwise take the
        // next one with it. Each body is found by its own offset, so this is
        // only worth saying out loud.
        const std::streamoff expected = block->second.offset
            + static_cast<std::streamoff>(block->second.length);
        if (static_cast<std::streamoff>(in.tellg()) != expected) {
            TraceLog(LOG_WARNING, "VOXELFILE: the %s body of grid %u is %u bytes, its loader read "
                                  "a different number", grid_header.grid_type.c_str(),
                     grid_header.id, block->second.length);
        }

        built_grid->name = grid_header.name;
        built_grid->description = grid_header.description;
        built_grid->set_transform(ctx.transform);
        built[grid_header.id] = built_grid;
    }

    // Both directions are written, so either one alone can put the tree back
    // together, and a file where they disagree gets a warning
    std::map<uint32_t, uint32_t> claimed_by;
    for (const GridHeader& grid_header : header.grids) {
        for (const uint32_t child_id : grid_header.children) {
            const auto claim = claimed_by.find(child_id);
            if (claim != claimed_by.end() && claim->second != grid_header.id) {
                TraceLog(LOG_WARNING, "VOXELFILE: grids %u and %u both claim %u as a child",
                         claim->second, grid_header.id, child_id);
                continue;
            }
            claimed_by[child_id] = grid_header.id;
        }
    }

    for (const GridHeader& grid_header : header.grids) {
        uint32_t parent_id = grid_header.parent_id;
        bool has_parent = grid_header.has_parent;

        const auto claim = claimed_by.find(grid_header.id);
        if (claim != claimed_by.end()) {
            if (!has_parent) {
                TraceLog(LOG_WARNING, "VOXELFILE: grid %u names no parent, taking %u from the "
                                      "children lists", grid_header.id, claim->second);
                parent_id = claim->second;
                has_parent = true;
            } else if (claim->second != parent_id) {
                TraceLog(LOG_WARNING, "VOXELFILE: grid %u says its parent is %u, but %u lists it "
                                      "as a child; going with %u",
                         grid_header.id, parent_id, claim->second, parent_id);
            }
        }
        if (!has_parent) continue;

        const auto parent = built.find(parent_id);
        if (parent == built.end()) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u hangs off %u, which is not in the file",
                     grid_header.id, parent_id);
            continue;
        }
        // set_parent refuses a loop, which is the one shape that would leave no
        // root at all
        if (!built[grid_header.id]->set_parent(parent->second)) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u cannot hang off %u", grid_header.id, parent_id);
        }
    }

    // Attaching comes after the whole tree is hung together: a grid is already
    // a child of its anchor by now, so only the connectors are left to name
    for (const GridHeader& grid_header : header.grids) {
        if (!grid_header.has_attachment) continue;

        VoxelGrid* g = built[grid_header.id];
        VoxelGrid* g_parent = g->get_parent();
        if (g_parent == nullptr) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u carries connector voxels but hangs off "
                                  "nothing, so it is not attached to anything", grid_header.id);
            continue;
        }

        // Snapping is off on purpose: the transform read out of the file is
        // where the grid was saved, and snapping would move it again from there
        if (!g->attach_to(g_parent, grid_header.anchor_voxel, grid_header.connector_voxel, false)) {
            TraceLog(LOG_WARNING, "VOXELFILE: grid %u could not be attached to its parent, it is "
                                  "only hanging off it", grid_header.id);
        }
    }

    // Whatever is left unparented. There should be exactly one.
    std::vector<VoxelGrid*> roots;
    for (const GridHeader& grid_header : header.grids) {
        VoxelGrid* g = built[grid_header.id];
        if (g->get_parent() == nullptr) roots.emplace_back(g);
    }

    if (roots.empty()) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' has no grid without a parent", path.c_str());
        delete_loose_grids(built);
        return nullptr;
    }
    for (size_t i = 1; i < roots.size(); ++i) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' holds a second tree under grid '%s', dropping it",
                 path.c_str(), roots[i]->name.c_str());
        delete_grid_tree(roots[i]);
    }

    if (out_header != nullptr) *out_header = header;

    std::vector<VoxelGrid*> loaded;
    collect_grids(roots.front(), &loaded);
    TraceLog(LOG_INFO, "VOXELFILE: loaded %s '%s' and %zu grid(s) below it from '%s'",
             roots.front()->get_grid_type().c_str(), roots.front()->name.c_str(),
             loaded.size() - 1, path.c_str());
    return roots.front();
}

void voxel_file::collect_grids(VoxelGrid* root, std::vector<VoxelGrid*>* out) {
    if (root == nullptr || out == nullptr) return;
    out->emplace_back(root);
    for (VoxelGrid* child : root->get_children()) {
        collect_grids(child, out);
    }
}

void voxel_file::delete_grid_tree(VoxelGrid* root) {
    if (root == nullptr) return;
    // The list is copied first: ~VoxelGrid detaches the node from its parent,
    // which rewrites the very vector this walks
    const std::vector<VoxelGrid*> children = root->get_children();
    for (VoxelGrid* child : children) {
        delete_grid_tree(child);
    }
    delete root;
}

// ------------------------------------------------------------ the voxel format

bool voxel_file::write_chunk(std::ostream& out, const VoxelChunk& chunk) {
    // Voxel data is mostly long stretches of one id, so the runs are counted
    // first and the raw bytes only win when the chunk is genuinely noisy
    std::vector<std::pair<uint16_t, VoxelID>> runs;
    for (size_t i = 0; i < CHUNK_VOLUME; ++i) {
        if (!runs.empty() && runs.back().second == chunk[i] && runs.back().first < MAX_RUN) {
            runs.back().first++;
        } else {
            runs.emplace_back(1, chunk[i]);
        }
    }

    const size_t rle_bytes = runs.size() * 3;
    if (rle_bytes < CHUNK_VOLUME) {
        write_u8(out, ENCODING_RLE);
        write_u32(out, static_cast<uint32_t>(rle_bytes));
        for (const auto& [length, id] : runs) {
            write_u16(out, length);
            write_u8(out, id);
        }
    } else {
        write_u8(out, ENCODING_RAW);
        write_u32(out, static_cast<uint32_t>(CHUNK_VOLUME));
        out.write(reinterpret_cast<const char*>(chunk.data()), CHUNK_VOLUME);
    }
    return out.good();
}

bool voxel_file::read_chunk(std::istream& in, VoxelChunk* out) {
    if (out == nullptr) return false;

    uint8_t encoding = 0;
    uint32_t payload_bytes = 0;
    if (!read_u8(in, &encoding) || !read_u32(in, &payload_bytes)) return false;

    out->fill(0);

    if (encoding == ENCODING_RAW) {
        if (payload_bytes != CHUNK_VOLUME) {
            TraceLog(LOG_WARNING, "VOXELFILE: raw chunk of %u bytes, expected %zu",
                     payload_bytes, CHUNK_VOLUME);
            return false;
        }
        in.read(reinterpret_cast<char*>(out->data()), CHUNK_VOLUME);
        return in.gcount() == static_cast<std::streamsize>(CHUNK_VOLUME);
    }

    if (encoding == ENCODING_RLE) {
        if (payload_bytes % 3 != 0) {
            TraceLog(LOG_WARNING, "VOXELFILE: run length chunk of %u bytes is not whole runs",
                     payload_bytes);
            return false;
        }
        size_t written = 0;
        for (uint32_t i = 0; i < payload_bytes / 3; ++i) {
            uint16_t length = 0;
            uint8_t id = 0;
            if (!read_u16(in, &length) || !read_u8(in, &id)) return false;
            // A corrupt run must not be allowed to write past the chunk
            if (written + length > CHUNK_VOLUME) {
                TraceLog(LOG_WARNING, "VOXELFILE: run length chunk overruns the chunk");
                return false;
            }
            for (uint16_t j = 0; j < length; ++j) (*out)[written++] = id;
        }
        if (written != CHUNK_VOLUME) {
            TraceLog(LOG_WARNING, "VOXELFILE: run length chunk covers %zu of %zu voxels",
                     written, CHUNK_VOLUME);
            return false;
        }
        return true;
    }

    TraceLog(LOG_WARNING, "VOXELFILE: unknown chunk encoding %u", encoding);
    return false;
}

// -------------------------------------------------------- little endian scalars

void voxel_file::write_u8(std::ostream& out, const uint8_t v) {
    out.put(static_cast<char>(v));
}

void voxel_file::write_u16(std::ostream& out, const uint16_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>(v >> 8 & 0xFF));
}

void voxel_file::write_u32(std::ostream& out, const uint32_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>(v >> 8 & 0xFF));
    out.put(static_cast<char>(v >> 16 & 0xFF));
    out.put(static_cast<char>(v >> 24 & 0xFF));
}

void voxel_file::write_i32(std::ostream& out, const int32_t v) {
    write_u32(out, static_cast<uint32_t>(v));
}

void voxel_file::write_f32(std::ostream& out, const float v) {
    // Written as the bit pattern, so the file does not depend on how this
    // machine happens to lay out a float in memory
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    write_u32(out, bits);
}

bool voxel_file::read_u8(std::istream& in, uint8_t* out) {
    const int c = in.get();
    if (c == std::char_traits<char>::eof()) return false;
    *out = static_cast<uint8_t>(c);
    return true;
}

bool voxel_file::read_u16(std::istream& in, uint16_t* out) {
    uint8_t b[2];
    if (!read_u8(in, &b[0]) || !read_u8(in, &b[1])) return false;
    *out = static_cast<uint16_t>(b[0] | b[1] << 8);
    return true;
}

bool voxel_file::read_u32(std::istream& in, uint32_t* out) {
    uint8_t b[4];
    for (uint8_t& byte : b) {
        if (!read_u8(in, &byte)) return false;
    }
    *out = static_cast<uint32_t>(b[0])
         | static_cast<uint32_t>(b[1]) << 8
         | static_cast<uint32_t>(b[2]) << 16
         | static_cast<uint32_t>(b[3]) << 24;
    return true;
}

bool voxel_file::read_i32(std::istream& in, int32_t* out) {
    uint32_t bits = 0;
    if (!read_u32(in, &bits)) return false;
    *out = static_cast<int32_t>(bits);
    return true;
}

bool voxel_file::read_f32(std::istream& in, float* out) {
    uint32_t bits = 0;
    if (!read_u32(in, &bits)) return false;
    std::memcpy(out, &bits, sizeof(bits));
    return true;
}
