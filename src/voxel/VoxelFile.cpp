//
// Created by Andrei Ghita on 26.08.2026.
//

#include "voxel/VoxelFile.hpp"

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

/** The loaders load_grid() dispatches to, keyed by the grid type in the header. */
std::map<std::string, voxel_file::GridLoader>& registry() {
    static std::map<std::string, voxel_file::GridLoader> loaders = {
        {VOXEL_MAP_STR, &VoxelMap::load_body},
        {SINGLE_CHUNK_GRID_STR, &SingleChunkGrid::load_body},
    };
    return loaders;
}

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

/**
 * Reads the header from an already open stream, leaving it on the first byte
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
    if (version > voxel_file::FORMAT_VERSION) {
        TraceLog(LOG_WARNING, "VOXELFILE: file is version %i, this build only reads up to %i",
                 version, voxel_file::FORMAT_VERSION);
        return false;
    }
    out->version = version;
    out->fields.clear();

    bool found_marker = false;
    while (read_line(in, &line)) {
        if (line == voxel_file::BINARY_MARKER) {
            found_marker = true;
            break;
        }
        if (line.empty() || line[0] == '#') continue;

        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            TraceLog(LOG_WARNING, "VOXELFILE: header line without a ':' ignored: %s", line.c_str());
            continue;
        }
        out->fields[trim(line.substr(0, colon))] = unescape(trim(line.substr(colon + 1)));
    }

    if (!found_marker) {
        TraceLog(LOG_WARNING, "VOXELFILE: header never ended, no '%s' line", voxel_file::BINARY_MARKER);
        return false;
    }

    if (const std::string* v = out->find("name")) out->name = *v;
    if (const std::string* v = out->find("description")) out->description = *v;
    if (const std::string* v = out->find("type")) out->grid_type = *v;

    if (out->grid_type.empty()) {
        TraceLog(LOG_WARNING, "VOXELFILE: header has no grid type");
        return false;
    }
    return true;
}

/** The part of the binary section that every grid type shares. */
void write_common(std::ostream& out, VoxelGrid* grid) {
    const auto* palette = grid->voxel_colours.get();
    const uint32_t palette_size = palette ? static_cast<uint32_t>(palette->size()) : 0;

    voxel_file::write_u32(out, palette_size);
    if (palette) {
        for (const auto& [id, colour] : *palette) {
            voxel_file::write_u8(out, id);
            voxel_file::write_u8(out, colour.r);
            voxel_file::write_u8(out, colour.g);
            voxel_file::write_u8(out, colour.b);
            voxel_file::write_u8(out, colour.a);
        }
    }

    // The local transform, not the world one: a grid saved while hanging off a
    // parent is stored where it sits inside that parent, so loading it back
    // under the same parent puts it in the same place
    const Transform t = grid->get_transform();
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

bool read_common(std::istream& in, VoxelColourMap* palette_out, Transform* transform_out) {
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
    *palette_out = palette;

    float v[10] = {};
    for (float& f : v) {
        if (!voxel_file::read_f32(in, &f)) return false;
    }
    *transform_out = Transform{
        Vector3{v[0], v[1], v[2]},
        Quaternion{v[3], v[4], v[5], v[6]},
        Vector3{v[7], v[8], v[9]},
    };
    return true;
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

    const auto* palette = grid->voxel_colours.get();

    // The readable header. '\n' is written explicitly, as the stream is in
    // binary mode and must not translate line endings.
    out << MAGIC << ' ' << FORMAT_VERSION << '\n';
    out << "name: " << escape(grid->name) << '\n';
    out << "description: " << escape(grid->description) << '\n';
    out << "type: " << grid->get_grid_type() << '\n';
    out << "voxel_bytes: " << sizeof(VoxelID) << '\n';
    out << "chunk_size: " << CHUNK_SIZE << '\n';
    out << "palette_size: " << (palette ? palette->size() : 0) << '\n';
    out << BINARY_MARKER << '\n';

    write_common(out, grid);

    if (!grid->write_body(out)) {
        TraceLog(LOG_WARNING, "VOXELFILE: %s failed to write its body to '%s'",
                 grid->get_grid_type().c_str(), path.c_str());
        return false;
    }

    out.flush();
    if (!out.good()) {
        TraceLog(LOG_WARNING, "VOXELFILE: writing '%s' failed", path.c_str());
        return false;
    }
    TraceLog(LOG_INFO, "VOXELFILE: saved %s to '%s'", grid->get_grid_type().c_str(), path.c_str());
    return true;
}

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

    const auto loader = registry().find(header.grid_type);
    if (loader == registry().end()) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' holds an unknown grid type '%s'",
                 path.c_str(), header.grid_type.c_str());
        return nullptr;
    }

    LoadContext ctx{};
    ctx.view = view;
    ctx.header = &header;
    if (!read_common(in, &ctx.palette, &ctx.transform)) {
        TraceLog(LOG_WARNING, "VOXELFILE: '%s' ends inside its common section", path.c_str());
        return nullptr;
    }
    // The caller may want the new grid on the same colours as the rest of the
    // scene, in which case the palette in the file is dropped
    if (shared_palette) ctx.palette = shared_palette;

    VoxelGrid* grid = loader->second(in, ctx);
    if (grid == nullptr) {
        TraceLog(LOG_WARNING, "VOXELFILE: could not read the %s body of '%s'",
                 header.grid_type.c_str(), path.c_str());
        return nullptr;
    }

    grid->name = header.name;
    grid->description = header.description;
    // Applied here rather than in the loaders, as it must land after the grid
    // is built and before it has any models
    grid->set_transform(ctx.transform);

    if (out_header != nullptr) *out_header = header;
    TraceLog(LOG_INFO, "VOXELFILE: loaded %s '%s' from '%s'",
             header.grid_type.c_str(), header.name.c_str(), path.c_str());
    return grid;
}

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
