//
// Created by Andrei Ghita on 09.10.2026.
//

#include "sim/Save.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>

namespace sim {

/** Reaches into a Simulation's world, which only this file is allowed to do. */
class SaveAccess {
public:
    static World& world(Simulation& simulation) { return simulation.world; }
    static const World& world(const Simulation& simulation) { return simulation.world; }
};

namespace {

std::string tag_name(const uint32_t tag) {
    std::string name(4, '?');
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((tag >> (8 * i)) & 0xFF);
        name[i] = (c >= 32 && c < 127) ? c : '?';
    }
    return name;
}

bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

/** One section: its header, then its payload, written on its own first so its length is known. */
template <typename F>
void write_section(ByteWriter& out, const uint32_t tag, const uint32_t version, F&& write_payload) {
    ByteWriter payload;
    write_payload(payload);
    out.write_u32(tag);
    out.write_u32(version);
    out.write_u32(static_cast<uint32_t>(payload.data().size()));
    out.write_bytes(payload.data());
}

/** What a section reader needs to know about one section it understands. */
struct SectionReader {
    uint32_t version;
    std::function<bool(ByteReader&)> read;
};

} // namespace

std::vector<uint8_t> write_save(const Simulation& simulation, const CommandQueue& commands) {
    const World& world = SaveAccess::world(simulation);

    ByteWriter out;
    out.write_u32(SAVE_MAGIC);
    out.write_u32(SAVE_FORMAT_VERSION);
    out.write_u32(4);

    // In the same order as Simulation::write_state(), with the same writers
    write_section(out, SECTION_CORE, SECTION_CORE_VERSION, [&world](ByteWriter& w) {
        w.write_u64(world.tick);
        world.rng.write(w);
        w.write_i32(world.water_level);
    });
    write_section(out, SECTION_ROUTES, SECTION_ROUTES_VERSION, [&world](ByteWriter& w) {
        world.routes.write(w);
    });
    write_section(out, SECTION_VEHICLES, SECTION_VEHICLES_VERSION, [&world](ByteWriter& w) {
        world.vehicles.write(w);
    });
    write_section(out, SECTION_COMMANDS, SECTION_COMMANDS_VERSION, [&commands](ByteWriter& w) {
        commands.write(w);
    });
    return out.data();
}

std::optional<LoadedGame> read_save(const std::span<const uint8_t> bytes, std::string* error) {
    // The seed does not matter: the Rng's whole state comes out of the save
    LoadedGame game{Simulation(0), CommandQueue{}};
    World& world = SaveAccess::world(game.simulation);

    const std::map<uint32_t, SectionReader> readers = {
        {SECTION_CORE, {SECTION_CORE_VERSION, [&world](ByteReader& in) {
            // A level no command could have set is refused, not trusted
            return in.read_u64(&world.tick) && world.rng.read(in)
                && in.read_i32(&world.water_level) && world.water_level >= MIN_WATER_LEVEL;
        }}},
        {SECTION_ROUTES, {SECTION_ROUTES_VERSION, [&world](ByteReader& in) {
            return world.routes.read(in);
        }}},
        {SECTION_VEHICLES, {SECTION_VEHICLES_VERSION, [&world](ByteReader& in) {
            return world.vehicles.read(in);
        }}},
        {SECTION_COMMANDS, {SECTION_COMMANDS_VERSION, [&game](ByteReader& in) {
            return game.commands.read(in);
        }}},
    };

    ByteReader in(bytes);
    uint32_t magic = 0, format = 0, count = 0;
    if (!in.read_u32(&magic) || magic != SAVE_MAGIC) {
        fail(error, "not a saved game");
        return std::nullopt;
    }
    if (!in.read_u32(&format) || format != SAVE_FORMAT_VERSION) {
        fail(error, "save format version " + std::to_string(format) + ", this build reads "
                    + std::to_string(SAVE_FORMAT_VERSION));
        return std::nullopt;
    }
    if (!in.read_u32(&count)) {
        fail(error, "the save ends in its header");
        return std::nullopt;
    }

    std::map<uint32_t, bool> seen;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t tag = 0, version = 0, length = 0;
        std::span<const uint8_t> payload;
        if (!in.read_u32(&tag) || !in.read_u32(&version) || !in.read_u32(&length)
            || !in.read_bytes(&payload, length)) {
            fail(error, "the save ends part way through section " + std::to_string(i));
            return std::nullopt;
        }

        const std::string name = tag_name(tag);
        const auto reader = readers.find(tag);
        if (reader == readers.end()) {
            fail(error, "unknown section " + name + ", written by a different build");
            return std::nullopt;
        }
        if (seen[tag]) {
            fail(error, "section " + name + " appears twice");
            return std::nullopt;
        }
        seen[tag] = true;
        if (version != reader->second.version) {
            fail(error, "section " + name + " is version " + std::to_string(version)
                        + ", this build reads " + std::to_string(reader->second.version));
            return std::nullopt;
        }

        ByteReader section(payload);
        if (!reader->second.read(section) || !section.at_end()) {
            fail(error, "section " + name + " is damaged");
            return std::nullopt;
        }
    }

    if (!in.at_end()) {
        fail(error, "bytes left over after the last section");
        return std::nullopt;
    }
    for (const auto& [tag, reader] : readers) {
        if (!seen[tag]) {
            fail(error, "section " + tag_name(tag) + " is missing");
            return std::nullopt;
        }
    }
    return std::move(game);
}

bool save_to_file(const std::string& path, const Simulation& simulation, const CommandQueue& commands,
                  std::string* error) {
    const std::vector<uint8_t> bytes = write_save(simulation, commands);

    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) return fail(error, "cannot create " + target.parent_path().string() + ": " + ec.message());

    // Written beside the target and renamed over it, so a save that fails part
    // way leaves the previous one standing rather than half overwritten
    const std::filesystem::path temp = target.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) return fail(error, "cannot write " + temp.string());
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) return fail(error, "cannot write " + temp.string());
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) return fail(error, "cannot replace " + target.string() + ": " + ec.message());
    return true;
}

std::optional<LoadedGame> load_from_file(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail(error, "cannot open " + path);
        return std::nullopt;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return read_save(bytes, error);
}

} // namespace sim
