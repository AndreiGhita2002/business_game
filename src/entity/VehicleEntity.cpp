//
// Created by Andrei Ghita on 04.10.2026.
//

#include "entity/VehicleEntity.hpp"

#include <cmath>
#include <raymath.h>

#include "entity/AssetRegistry.hpp"
#include "voxel/VoxelGrid.hpp"

VehicleEntity::VehicleEntity(const sim::VehicleId id, GridSink* sink, VoxelGrid* root,
                             const Vector3 pivot, const sim::Simulation& sim)
    : Entity(sink, root, pivot), id(id)
{
    read(sim, &current);
    previous = current;
    shown = current;
    place(current);
}

bool VehicleEntity::read(const sim::Simulation& sim, Pose* pose) {
    const sim::Vehicle* vehicle = sim.vehicles().get(id);
    if (vehicle == nullptr) return false;
    const std::optional<sim::RoutePose> route_pose = sim.vehicle_pose(id);
    if (!route_pose) return false;

    const bool reversed = vehicle->speed < sim::Fixed{};
    *pose = pose_from_route(*route_pose, reversed);
    speed = std::fabs(to_float(vehicle->speed)) / tick_seconds();
    return true;
}

void VehicleEntity::on_tick(const sim::Simulation& sim) {
    Pose next;
    // A vehicle that is gone stays where it was until the manager drops it
    if (!read(sim, &next)) return;
    previous = current;
    current = next;
}

void VehicleEntity::present(const float alpha, const float frame_dt) {
    shown = interpolate_pose(previous, current, alpha);
    place(shown);
    // The scripts run after the body is placed, as the wheels hang off it
    Entity::present(alpha, frame_dt);
}

// --- Wheels ---

WheelSpinScript::WheelSpinScript(const VehicleEntity* vehicle, std::vector<VoxelGrid*> wheels,
                                 const float radius)
    : vehicle(vehicle), wheels(std::move(wheels)), radius(radius) {}

void WheelSpinScript::on_update(const float delta) {
    if (radius <= 0.0f) return;

    // Rolling without slipping: the contact patch is still, so the wheel turns
    // at speed / radius. The car drives towards +X, and for that the turn
    // about +Z is negative (the top of the wheel goes forwards).
    angle -= vehicle->ground_speed() / radius * delta;
    angle = std::fmod(angle, 2.0f * PI);

    const Quaternion turn = QuaternionFromAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, angle);
    for (VoxelGrid* wheel : wheels) {
        Transform t = wheel->get_transform();
        t.rotation = turn;
        wheel->set_transform(t);
        wheel->snap_to_anchor();
    }
}

const std::string& WheelSpinScript::get_type() const {
    static const std::string TYPE = "WheelSpinScript";
    return TYPE;
}

std::vector<VoxelGrid*> find_wheels(const std::vector<VoxelGrid*>& grids) {
    std::vector<VoxelGrid*> wheels;
    for (VoxelGrid* grid : grids) {
        if (grid->name == WHEEL_GRID_NAME) wheels.push_back(grid);
    }
    return wheels;
}
