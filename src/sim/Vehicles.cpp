//
// Created by Andrei Ghita on 04.10.2026.
//

#include "sim/Vehicles.hpp"

namespace sim {

void Vehicles::advance(const Routes& routes) {
    pool.for_each([&routes](VehicleId, Vehicle& vehicle) {
        const Route* route = routes.get(vehicle.route);
        // Routes are never removed today, so this does not happen. Should that
        // change, a vehicle whose route is gone simply stops.
        if (route == nullptr) return;
        vehicle.distance = wrap(vehicle.distance + vehicle.speed, route->length());
    });
}

void Vehicles::write(ByteWriter& out) const {
    pool.write(out, [](ByteWriter& w, const Vehicle& v) {
        w.write_u32(v.route.index);
        w.write_u32(v.route.generation);
        w.write_fixed(v.distance);
        w.write_fixed(v.speed);
        w.write_string(v.model);
    });
}

} // namespace sim
