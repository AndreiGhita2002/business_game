//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_EVENT_HPP
#define BUSINESS_GAME_SIM_EVENT_HPP
#include <cstdint>
#include <variant>

#include "sim/Handle.hpp"

namespace sim {

/** Why a command was refused. None means it was not. */
enum class RejectReason : uint8_t {
    None = 0,
    InvalidRoute,     // the points do not make a route, see Routes::is_valid
    UnknownRoute,     // the route named does not exist
    UnknownVehicle,   // the vehicle named does not exist, or no longer does
    InvalidAsset,     // an empty or overlong AssetId
    WrongTick,        // stamped for a different tick than the one it was handed to
    NoCommand,        // a stamped command with nothing in it
    InvalidWaterLevel, // below the bottom of the world, see MIN_WATER_LEVEL
    InvalidIsland,    // the footprint is off the world or over land, see place_island()
};

const char* reject_reason_name(RejectReason reason);

// What one step() reports back. These are for the discrete changes the
// presentation has to react to; continuous state, like where a vehicle is, is
// read through the simulation's const accessors instead of being pushed.
// Nothing ever feeds an event back into the simulation.

struct RouteAdded { RouteId id; };
struct VehicleSpawned { VehicleId id; };
struct VehicleDespawned { VehicleId id; };

/**
 * An island was generated (PlaceIsland). The cells it covers are the
 * footprint of `shape` turned `rotation` times (island_footprint() in
 * sim/Island.hpp) with its corner at cell (cell_x, cell_y): the ones the
 * presentation has to draw again. Plain numbers, so this header does not need
 * the island code.
 */
struct IslandPlaced {
    int32_t cell_x;
    int32_t cell_y;
    uint8_t shape;
    uint8_t rotation;
};

/**
 * A command that was refused. Refusing happens inside the simulation, at the
 * tick the command runs, so every machine in a lockstep game refuses the same
 * commands the same way.
 */
struct CommandRejected {
    uint32_t player;
    uint32_t sequence;
    RejectReason reason;
};

using Event = std::variant<RouteAdded, VehicleSpawned, VehicleDespawned, IslandPlaced, CommandRejected>;

} // namespace sim

#endif //BUSINESS_GAME_SIM_EVENT_HPP
