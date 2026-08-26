//
// Created by Andrei Ghita on 26.08.2026.
//

#include "voxel/VoxelGrid.hpp"

#include <algorithm>

#include "game/main.hpp"

VoxelGrid::~VoxelGrid() {
    // The children outlive their parent, so each keeps the place it was in
    // rather than snapping back to wherever its local transform alone points
    for (VoxelGrid* child : children) {
        if (child == nullptr) continue;
        child->transform = child->get_world_transform();
        child->parent = nullptr;
    }
    children.clear();

    if (parent != nullptr) {
        parent->forget_child(this);
        parent = nullptr;
    }
}

Transform VoxelGrid::get_world_transform() const {
    if (parent == nullptr) return transform;
    // The parent is the one applied on top, so the local transform happens
    // first and the chain up to the root happens around it
    return transform_transform(transform, parent->get_world_transform());
}

bool VoxelGrid::set_parent(VoxelGrid* new_parent) {
    if (new_parent == parent) return true;

    // A grid that is its own ancestor would send get_world_transform() up a
    // chain with no root
    if (new_parent == this || (new_parent != nullptr && is_ancestor_of(new_parent))) {
        TraceLog(LOG_WARNING, "VOXELGRID: refused a parent that is already below this grid");
        return false;
    }

    if (parent != nullptr) parent->forget_child(this);
    parent = new_parent;
    if (parent != nullptr) parent->children.emplace_back(this);
    return true;
}

bool VoxelGrid::add_child(VoxelGrid* child) {
    if (child == nullptr) return false;
    return child->set_parent(this);
}

bool VoxelGrid::is_ancestor_of(const VoxelGrid* other) const {
    for (const VoxelGrid* node = other; node != nullptr; node = node->parent) {
        if (node->parent == this) return true;
    }
    return false;
}

void VoxelGrid::forget_child(const VoxelGrid* child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}
