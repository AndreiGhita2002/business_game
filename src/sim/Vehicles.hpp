//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_VEHICLES_HPP
#define BUSINESS_GAME_SIM_VEHICLES_HPP
#include <string>

#include "sim/Fixed.hpp"
#include "sim/Handle.hpp"
#include "sim/Pool.hpp"
#include "sim/Routes.hpp"
#include "sim/Serial.hpp"

namespace sim {

/**
 * What a simulation object looks like, by name, e.g. "car". The simulation
 * never opens it: the presentation's AssetRegistry turns it into voxel grids.
 * That is what keeps the voxels cosmetic - a client with an edited or stale
 * asset draws something different and still simulates the same thing.
 *
 * A string for now. It can become an interned number once a network has to
 * carry a lot of them.
 */
using AssetId = std::string;

// Longest AssetId a command or a save will carry
constexpr uint32_t MAX_ASSET_ID_LENGTH = 64;

/** A vehicle driving round a route. Placeholder for the architecture slice. */
struct Vehicle {
    RouteId route;
    // How far along the route from its first point, always in [0, length)
    Fixed distance;
    // Units per tick. Negative runs the route backwards.
    Fixed speed;
    AssetId model;
};

/** Every vehicle in the simulation. */
class Vehicles {
public:
    VehicleId add(Vehicle vehicle) { return pool.insert(std::move(vehicle)); }
    bool remove(VehicleId id) { return pool.erase(id); }

    bool contains(VehicleId id) const { return pool.contains(id); }
    Vehicle* get(VehicleId id) { return pool.get(id); }
    const Vehicle* get(VehicleId id) const { return pool.get(id); }
    size_t size() const { return pool.size(); }

    /** One tick of driving: every vehicle moves `speed` along its route. */
    void advance(const Routes& routes);

    template <typename F>
    void for_each(F&& f) const { pool.for_each(std::forward<F>(f)); }

    void write(ByteWriter& out) const;

private:
    Pool<Vehicle, VehicleTag> pool;
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_VEHICLES_HPP
