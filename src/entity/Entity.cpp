//
// Created by Andrei Ghita on 04.10.2026.
//

#include "entity/Entity.hpp"

#include <raymath.h>

#include "voxel/VoxelFile.hpp"
#include "voxel/VoxelGrid.hpp"

Entity::Entity(GridSink* sink, VoxelGrid* root, const Vector3 pivot)
    : sink(sink), pivot(pivot)
{
    voxel_file::collect_grids(root, &grids);
    if (sink != nullptr) sink->add_grids(grids);
}

Entity::~Entity() {
    // Scripts first, as they hold pointers to the grids
    scripts.clear();

    if (sink != nullptr) sink->remove_grids(grids);

    // Children before parents. Only the grids this entity was made with go:
    // delete_grid_tree() would also take anything attached to them since,
    // which belongs to someone else and keeps its place when its parent goes.
    for (auto it = grids.rbegin(); it != grids.rend(); ++it) delete *it;
    grids.clear();
}

void Entity::present(float /*alpha*/, const float frame_dt) {
    for (const auto& script : scripts) script->on_update(frame_dt);
}

void Entity::add_script(std::unique_ptr<Script> script) {
    Script* added = script.get();
    scripts.push_back(std::move(script));
    added->on_start();
}

void Entity::place(const Pose& pose) {
    VoxelGrid* root = root_grid();
    Transform t = root->get_transform();
    t.rotation = pose.rotation;
    // A model point p lands at rotation * (scale * p) + translation, so this
    // is the translation that puts the pivot on the pose's position
    const Vector3 scaled_pivot = Vector3Multiply(pivot, t.scale);
    t.translation = Vector3Subtract(pose.position, Vector3RotateByQuaternion(scaled_pivot, pose.rotation));
    root->set_transform(t);
}
