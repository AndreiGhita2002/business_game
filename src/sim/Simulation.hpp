//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_SIMULATION_HPP
#define BUSINESS_GAME_SIM_SIMULATION_HPP
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "sim/Command.hpp"
#include "sim/Event.hpp"
#include "sim/Rng.hpp"
#include "sim/Routes.hpp"
#include "sim/Vehicles.hpp"

namespace sim {

/**
 * How many ticks the simulation runs per second of game time at normal speed.
 * An integer: the simulation counts ticks and never seconds, and the
 * presentation turns this into a tick length for itself.
 */
constexpr int TICKS_PER_SECOND = 20;

/**
 * Everything the simulation is, as something that can be changed. Only a
 * Command ever holds one of these, inside Simulation::step(): from outside,
 * the state is reachable through Simulation's const accessors and nothing
 * else.
 */
class World {
public:
    uint64_t tick = 0;
    Rng rng;
    Routes routes;
    Vehicles vehicles;

    void emit(Event event) { events.push_back(std::move(event)); }

private:
    friend class Simulation;
    // What the step in progress has emitted
    std::vector<Event> events;
};

/**
 * The game's single source of truth.
 *
 * Deterministic by construction: the same seed and the same stamped commands
 * give the same state, tick for tick, on every machine. That holds because
 * the state is integers only (Fixed for anything fractional), every container
 * is walked in a fixed order, randomness comes from the seeded Rng, and
 * nothing here knows about frames, the camera or what is on screen. Keep it
 * that way: this library does not link raylib, and the build fails if
 * something under src/sim includes it.
 *
 * The presentation reads it through the const accessors between steps, and
 * changes it only by queueing commands.
 */
class Simulation {
public:
    explicit Simulation(uint64_t seed);

    /**
     * Runs tick tick(): applies `commands` sorted by (player, sequence), then
     * advances every system by one tick, then counts the tick as done.
     * A command stamped for any other tick is refused with WrongTick.
     */
    void step(std::span<const StampedCommand> commands);

    /** How many ticks have completed, which is also the tick the next step() runs. */
    uint64_t tick() const { return world.tick; }

    /** What the last step() emitted, in the order it happened. */
    std::span<const Event> events() const { return world.events; }

    const Routes& routes() const { return world.routes; }
    const Vehicles& vehicles() const { return world.vehicles; }

    /** Where a vehicle is, or nothing when it does not exist. */
    std::optional<RoutePose> vehicle_pose(VehicleId id) const;

    /**
     * A hash of the whole state. Two simulations agree on it exactly when they
     * agree on everything, which is what a desync check compares, and what the
     * determinism tests lean on.
     */
    uint64_t checksum() const;

    /**
     * The whole state as bytes, in a fixed order. The checksum is taken over
     * this, so anything left out of it is invisible to desync checks.
     * Saving will be built on it; there is no reading it back yet.
     */
    void write_state(ByteWriter& out) const;

private:
    World world;
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_SIMULATION_HPP
