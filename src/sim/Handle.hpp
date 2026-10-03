//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_HANDLE_HPP
#define BUSINESS_GAME_SIM_HANDLE_HPP
#include <cstdint>

namespace sim {

/**
 * The name of something in the simulation: a slot in a Pool, plus which
 * occupant of that slot it means.
 *
 * Everything outside the simulation - commands, events, the presentation -
 * holds one of these and never a pointer. A pointer into a pool would dangle
 * the moment the pool grew, and could not be sent over a network or saved. The
 * generation is what stops a handle to a removed vehicle from quietly meaning
 * whatever vehicle took its slot afterwards.
 *
 * `Tag` only keeps the kinds apart, so a VehicleId cannot be passed where a
 * RouteId is wanted. A default constructed handle never names anything, as
 * generations start at 1.
 */
template <typename Tag>
struct Handle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;

    bool is_null() const { return generation == 0; }

    /** Both halves in one number, for using a handle as a map key. */
    uint64_t key() const { return (static_cast<uint64_t>(index) << 32) | generation; }

    bool operator==(const Handle&) const = default;
};

using VehicleId = Handle<struct VehicleTag>;
using RouteId = Handle<struct RouteTag>;

} // namespace sim

#endif //BUSINESS_GAME_SIM_HANDLE_HPP
