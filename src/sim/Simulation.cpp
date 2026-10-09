//
// Created by Andrei Ghita on 04.10.2026.
//

#include "sim/Simulation.hpp"

#include <algorithm>

namespace sim {

Simulation::Simulation(const uint64_t seed, const TerrainSettings& terrain) {
    world.rng = Rng(seed);
    world.terrain = generate_terrain(terrain);
    world.water_level = terrain.water_level;
}

void Simulation::step(const std::span<const StampedCommand> commands) {
    world.events.clear();

    // The order commands arrived in is not part of the game: two machines can
    // receive the same commands in different orders. (player, sequence) is an
    // order every machine agrees on.
    std::vector<const StampedCommand*> ordered;
    ordered.reserve(commands.size());
    for (const StampedCommand& c : commands) ordered.push_back(&c);
    std::sort(ordered.begin(), ordered.end(), [](const StampedCommand* a, const StampedCommand* b) {
        if (a->player != b->player) return a->player < b->player;
        return a->sequence < b->sequence;
    });

    for (const StampedCommand* c : ordered) {
        RejectReason reason;
        if (c->command == nullptr) reason = RejectReason::NoCommand;
        else if (c->tick != world.tick) reason = RejectReason::WrongTick;
        else reason = c->command->apply(world);

        if (reason != RejectReason::None) {
            world.emit(CommandRejected{c->player, c->sequence, reason});
        }
    }

    // The systems, in a fixed order. Only one so far.
    world.vehicles.advance(world.routes);

    world.tick++;
}

std::optional<RoutePose> Simulation::vehicle_pose(const VehicleId id) const {
    const Vehicle* vehicle = world.vehicles.get(id);
    if (vehicle == nullptr) return std::nullopt;
    return world.routes.pose_at(vehicle->route, vehicle->distance);
}

void Simulation::write_state(ByteWriter& out) const {
    out.write_u64(world.tick);
    world.rng.write(out);
    out.write_i32(world.water_level);
    world.terrain.write(out);
    world.routes.write(out);
    world.vehicles.write(out);
}

uint64_t Simulation::checksum() const {
    ByteWriter out;
    write_state(out);
    return fnv1a(out.data());
}

const char* reject_reason_name(const RejectReason reason) {
    switch (reason) {
        case RejectReason::None: return "none";
        case RejectReason::InvalidRoute: return "invalid route";
        case RejectReason::UnknownRoute: return "unknown route";
        case RejectReason::UnknownVehicle: return "unknown vehicle";
        case RejectReason::InvalidAsset: return "invalid asset";
        case RejectReason::WrongTick: return "wrong tick";
        case RejectReason::NoCommand: return "no command";
        case RejectReason::InvalidWaterLevel: return "invalid water level";
        case RejectReason::InvalidIsland: return "invalid island";
    }
    return "?";
}

} // namespace sim
