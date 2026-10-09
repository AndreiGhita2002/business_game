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
#include "sim/Terrain.hpp"
#include "sim/Vehicles.hpp"

namespace sim {

/**
 * How many ticks the simulation runs per second of game time at normal speed.
 * An integer: the simulation counts ticks and never seconds, and the
 * presentation turns this into a tick length for itself.
 */
constexpr int TICKS_PER_SECOND = 20;

/**
 * The water level a new game starts with, and the lowest one there can be.
 * A level is a voxel layer: the water fills layers 0 to the level, one layer
 * per unit of z. 0 is the lowest because layer 0 is the bottom of the world.
 */
constexpr int32_t DEFAULT_WATER_LEVEL = 1;
constexpr int32_t MIN_WATER_LEVEL = 0;

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
    // The ground, in blocks. Nothing changes it after it is generated yet.
    Terrain terrain;
    Routes routes;
    Vehicles vehicles;
    // The highest voxel layer the sea fills. Nothing in the simulation reads
    // it yet; it is here so that changing it is a command like any other, is
    // saved, and is the same on every machine.
    int32_t water_level = DEFAULT_WATER_LEVEL;

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
    /** A new game: the Rng seeded, and the terrain generated from `terrain`. */
    explicit Simulation(uint64_t seed, const TerrainSettings& terrain = TerrainSettings{});

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

    const Terrain& terrain() const { return world.terrain; }
    const Routes& routes() const { return world.routes; }
    const Vehicles& vehicles() const { return world.vehicles; }
    int32_t water_level() const { return world.water_level; }

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
     *
     * A saved game (sim/Save.hpp) writes the same systems with the same
     * writers, a section each, so the two cannot disagree about what the
     * state is.
     */
    void write_state(ByteWriter& out) const;

private:
    // The one thing outside a command allowed to change the world: reading a
    // saved game back into it, see sim/Save.cpp
    friend class SaveAccess;

    World world;
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_SIMULATION_HPP
