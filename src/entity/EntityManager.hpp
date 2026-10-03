//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_ENTITYMANAGER_HPP
#define BUSINESS_GAME_ENTITYMANAGER_HPP
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include <raylib.h>

#include "entity/AssetRegistry.hpp"
#include "entity/GridSink.hpp"
#include "entity/VehicleEntity.hpp"
#include "sim/Simulation.hpp"

class VoxelView;

// Default radii, in world units, see EntityManager::realize_radius
constexpr float DEFAULT_REALIZE_RADIUS = 56.0f;
constexpr float DEFAULT_UNREALIZE_RADIUS = 64.0f;

/**
 * Whether a simulation object at `distance` from the camera should have an
 * entity, given whether it has one now. The gap between the two radii is what
 * stops an object sitting on the boundary from being built and torn down on
 * alternate frames.
 */
bool keep_realized(bool realized, float distance, float realize_radius, float unrealize_radius);

/**
 * Keeps an entity for every simulation object near the camera, and none for
 * the rest.
 *
 * The simulation runs everything everywhere; this decides what is worth
 * drawing. Being realised or not is purely a presentation matter and nothing
 * flows back: two players looking at different parts of the map see different
 * entities and run identical simulations.
 *
 * Driven by the game loop: on_tick() after every simulation step, present()
 * once per frame. Only vehicles exist so far.
 */
class EntityManager {
public:
    // An object nearer than this to the camera gets an entity...
    float realize_radius = DEFAULT_REALIZE_RADIUS;
    // ...and one further than this loses it
    float unrealize_radius = DEFAULT_UNREALIZE_RADIUS;

    /**
     * @param sink: where the entities' grids are drawn.
     * @param assets: turns a vehicle's model name into grids.
     * @param view: handed to the grids as they are made, null in the tests.
     */
    EntityManager(GridSink* sink, const AssetRegistry* assets, VoxelView* view);
    ~EntityManager();

    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;

    /** After every step: drop entities for what was removed, and snapshot the rest. */
    void on_tick(const sim::Simulation& sim);

    /**
     * Every frame: realise what has come into range, unrealise what has left
     * it, and place every entity between the last two ticks.
     * @param camera: the camera's position in world space.
     * @param alpha: how far the frame is between the last two ticks, 0 to 1.
     */
    void present(const sim::Simulation& sim, Vector3 camera, float alpha, float frame_dt);

    /** Drops every entity. The next present() builds the ones in range again. */
    void clear();

    /** The entity for a vehicle, or null while it has none. */
    VehicleEntity* vehicle(sim::VehicleId id) const;

    /** The vehicle entity a grid belongs to, any of its grids, or null. For picking. */
    VehicleEntity* vehicle_for_grid(const VoxelGrid* grid) const;

    size_t realized_count() const { return vehicles.size(); }

private:
    GridSink* sink;
    const AssetRegistry* assets;
    VoxelView* view;

    // Keyed by Handle::key(). Ordered only so that walking it is repeatable,
    // which makes a bug easier to chase; nothing depends on the order.
    std::map<uint64_t, std::unique_ptr<VehicleEntity>> vehicles;
    std::unordered_map<const VoxelGrid*, VehicleEntity*> by_grid;

    // Models that failed to instantiate, so each is complained about once
    // rather than every frame
    std::set<sim::AssetId> missing_assets;

    void realize(const sim::Simulation& sim, sim::VehicleId id, const sim::Vehicle& vehicle);
    void unrealize(sim::VehicleId id);
};

#endif //BUSINESS_GAME_ENTITYMANAGER_HPP
