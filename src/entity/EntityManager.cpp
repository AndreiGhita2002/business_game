//
// Created by Andrei Ghita on 04.10.2026.
//

#include "entity/EntityManager.hpp"

#include <raymath.h>
#include <variant>
#include <vector>

bool keep_realized(const bool realized, const float distance,
                   const float realize_radius, const float unrealize_radius) {
    return realized ? distance <= unrealize_radius : distance <= realize_radius;
}

EntityManager::EntityManager(GridSink* sink, const AssetRegistry* assets, VoxelView* view)
    : sink(sink), assets(assets), view(view) {}

EntityManager::~EntityManager() {
    clear();
}

void EntityManager::on_tick(const sim::Simulation& sim) {
    for (const sim::Event& event : sim.events()) {
        if (const auto* gone = std::get_if<sim::VehicleDespawned>(&event)) {
            unrealize(gone->id);
        }
        // New vehicles need nothing here: present() looks at every vehicle
        // anyway, and realises the new ones that are in range
    }

    for (const auto& [key, entity] : vehicles) entity->on_tick(sim);
}

void EntityManager::present(const sim::Simulation& sim, const Vector3 camera,
                            const float alpha, const float frame_dt) {
    // What should be realised. Decided on the newest tick's positions rather
    // than the interpolated ones: a tick's lag makes no difference to whether
    // something is near the camera.
    std::vector<std::pair<sim::VehicleId, const sim::Vehicle*>> to_realize;
    std::vector<sim::VehicleId> to_unrealize;

    // TODO(claude): after the next vehicle pass (Andrei, performance review).
    //  This scan runs every frame over every vehicle in the simulation, near or
    //  not. Poses only change on a tick, so it only needs running on a frame
    //  that ran a tick or moved the camera a few units; beyond that, spatial
    //  buckets so only the vehicles near the camera are looked at.
    sim.vehicles().for_each([&](const sim::VehicleId id, const sim::Vehicle& vehicle) {
        const std::optional<sim::RoutePose> pose = sim.vehicle_pose(id);
        if (!pose) return;
        const float distance = Vector3Distance(camera, sim_to_world(pose->position));

        const bool realized = vehicles.contains(id.key());
        const bool keep = keep_realized(realized, distance, realize_radius, unrealize_radius);
        if (keep && !realized) to_realize.emplace_back(id, &vehicle);
        if (!keep && realized) to_unrealize.push_back(id);
    });

    // Entities whose vehicle is gone without a despawn event reaching
    // on_tick(). Cannot happen while on_tick() runs every step, but cheap to
    // be sure of, and it is what tidies up after the simulation is replaced.
    for (const auto& [key, entity] : vehicles) {
        if (!sim.vehicles().contains(entity->vehicle_id())) to_unrealize.push_back(entity->vehicle_id());
    }

    // TODO(claude): after the next vehicle pass. Each unrealize() hands its
    //  grids back through its own VoxelView::remove_grids() call, which is
    //  O(all grids) each time: gather every leaving vehicle's grids and remove
    //  them in one call. And cap how many are realised in one frame, nearest
    //  the camera first, so panning across a busy area does not build dozens
    //  of vehicles (and mesh five grids each) in a single frame.
    for (const sim::VehicleId id : to_unrealize) unrealize(id);
    for (const auto& [id, vehicle] : to_realize) realize(sim, id, *vehicle);

    for (const auto& [key, entity] : vehicles) entity->present(alpha, frame_dt);
}

void EntityManager::clear() {
    by_grid.clear();
    vehicles.clear();
}

VehicleEntity* EntityManager::vehicle(const sim::VehicleId id) const {
    const auto it = vehicles.find(id.key());
    return it == vehicles.end() ? nullptr : it->second.get();
}

VehicleEntity* EntityManager::vehicle_for_grid(const VoxelGrid* grid) const {
    const auto it = by_grid.find(grid);
    return it == by_grid.end() ? nullptr : it->second;
}

void EntityManager::realize(const sim::Simulation& sim, const sim::VehicleId id,
                            const sim::Vehicle& vehicle) {
    if (assets == nullptr) return;

    Vector3 pivot{};
    // TODO(claude): after the next vehicle pass. Every realisation builds the
    //  asset from scratch (a file asset would be read from disk again), then
    //  meshes and uploads each of its grids and takes an atlas brick for each.
    //  One prototype per asset, its meshes and bricks shared by every instance
    //  and only the transforms per vehicle, would make realising nearly free.
    VoxelGrid* root = assets->instantiate(vehicle.model, view, &pivot);
    if (root == nullptr) {
        if (missing_assets.insert(vehicle.model).second) {
            TraceLog(LOG_WARNING, "ENTITY: no asset called \"%s\", vehicles using it are not drawn",
                     vehicle.model.c_str());
        }
        return;
    }

    // Scaled to fit before the entity places it, which takes the scale into
    // account when it puts the pivot on the simulation's position
    float scale = 1.0f;
    // TODO(claude): after the next vehicle pass. The same for every vehicle of
    //  an asset, and it walks every voxel of every grid: cache it per AssetId.
    const float extent = largest_extent(root);
    if (vehicle_size > 0.0f && extent > 0.0f) scale = vehicle_size / extent;
    Transform root_transform = root->get_transform();
    root_transform.scale = Vector3{scale, scale, scale};
    root->set_transform(root_transform);

    auto entity = std::make_unique<VehicleEntity>(id, sink, root, pivot, sim);
    const std::vector<VoxelGrid*> wheels = find_wheels(entity->owned_grids());
    if (!wheels.empty()) {
        // The wheels shrink with the rest of the car, and a smaller wheel has
        // to turn faster to cover the same ground: the script wants the
        // radius in world units, the same units as the speed
        entity->add_script(std::make_unique<WheelSpinScript>(entity.get(), wheels,
                                                             PLACEHOLDER_WHEEL_RADIUS * scale));
    }

    for (const VoxelGrid* grid : entity->owned_grids()) by_grid[grid] = entity.get();
    vehicles[id.key()] = std::move(entity);
}

void EntityManager::unrealize(const sim::VehicleId id) {
    const auto it = vehicles.find(id.key());
    if (it == vehicles.end()) return;
    for (const VoxelGrid* grid : it->second->owned_grids()) by_grid.erase(grid);
    // The entity's destructor takes its grids out of the sink and deletes them
    vehicles.erase(it);
}
