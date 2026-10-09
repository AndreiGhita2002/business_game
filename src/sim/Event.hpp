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
 * A command that was refused. Refusing happens inside the simulation, at the
 * tick the command runs, so every machine in a lockstep game refuses the same
 * commands the same way.
 */
struct CommandRejected {
    uint32_t player;
    uint32_t sequence;
    RejectReason reason;
};

using Event = std::variant<RouteAdded, VehicleSpawned, VehicleDespawned, CommandRejected>;

} // namespace sim

#endif //BUSINESS_GAME_SIM_EVENT_HPP
