//
// Created by Andrei Ghita on 09.10.2026.
//

#ifndef BUSINESS_GAME_SIM_SAVE_HPP
#define BUSINESS_GAME_SIM_SAVE_HPP
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sim/Command.hpp"
#include "sim/Simulation.hpp"

namespace sim {

/**
 * Saved games: the simulation and the commands still waiting to run, and
 * nothing else. Everything on screen is rebuilt from the simulation after a
 * load, the way EntityManager rebuilds after clear(), so the presentation has
 * nothing of its own to save.
 *
 * Layout, little endian throughout:
 *
 *   u32 magic            "BGSV"
 *   u32 format_version   SAVE_FORMAT_VERSION, the layout of this frame
 *   u32 section_count
 *   then each section:
 *     u32 tag            four characters naming a system, e.g. "ROUT"
 *     u32 version        that section's own layout version
 *     u32 payload_bytes
 *     payload
 *
 * One section per system, each with its own version, so a system's layout can
 * change without touching the others. There is no migration: a save from a
 * different version of any section, or with a section this build does not
 * know, is refused with a message saying which. Every known section has to be
 * there exactly once, and each payload has to be read to its last byte.
 *
 * The sections are written with the same writers Simulation::write_state()
 * uses for the checksum, which is how a loaded game is known to be the game
 * that was saved: the two checksums are equal.
 */

constexpr uint32_t SAVE_FORMAT_VERSION = 1;

/** Four characters as a tag, laid out so a hex dump of the file reads them in order. */
constexpr uint32_t section_tag(const char (&name)[5]) {
    return static_cast<uint32_t>(static_cast<uint8_t>(name[0]))
         | static_cast<uint32_t>(static_cast<uint8_t>(name[1])) << 8
         | static_cast<uint32_t>(static_cast<uint8_t>(name[2])) << 16
         | static_cast<uint32_t>(static_cast<uint8_t>(name[3])) << 24;
}

constexpr uint32_t SAVE_MAGIC = section_tag("BGSV");

// The sections and the version of each this build writes and reads. Bump a
// version whenever its layout changes; older saves of it then stop loading.
constexpr uint32_t SECTION_CORE = section_tag("CORE");      // the tick and the Rng
constexpr uint32_t SECTION_CORE_VERSION = 1;
constexpr uint32_t SECTION_ROUTES = section_tag("ROUT");
constexpr uint32_t SECTION_ROUTES_VERSION = 1;
constexpr uint32_t SECTION_VEHICLES = section_tag("VEHI");
constexpr uint32_t SECTION_VEHICLES_VERSION = 1;
constexpr uint32_t SECTION_COMMANDS = section_tag("CMDS");  // the CommandQueue
constexpr uint32_t SECTION_COMMANDS_VERSION = 1;

/** What a load gives back. Both are meant to replace the game's own. */
struct LoadedGame {
    Simulation simulation;
    CommandQueue commands;
};

/** The game as bytes. */
std::vector<uint8_t> write_save(const Simulation& simulation, const CommandQueue& commands);

/**
 * The game written by write_save(), or nothing, with `error` (when given)
 * saying what was wrong. Nothing is half loaded: either every section reads
 * cleanly or nothing comes back.
 */
std::optional<LoadedGame> read_save(std::span<const uint8_t> bytes, std::string* error = nullptr);

/** write_save() to a file, creating its directory if need be. */
bool save_to_file(const std::string& path, const Simulation& simulation, const CommandQueue& commands,
                  std::string* error = nullptr);

/** read_save() from a file. */
std::optional<LoadedGame> load_from_file(const std::string& path, std::string* error = nullptr);

} // namespace sim

#endif //BUSINESS_GAME_SIM_SAVE_HPP
