//
// Created by Andrei Ghita on 26.08.2026.
//

#include "voxel/VoxelGrid.hpp"

#include <algorithm>
#include <raymath.h>

#include "game/Transform.hpp"

VoxelGrid::~VoxelGrid() {
    // The children outlive their parent, so each keeps the place it was in
    // rather than snapping back to wherever its local transform alone points
    for (VoxelGrid* child : children) {
        if (child == nullptr) continue;
        child->transform = child->get_world_transform();
        child->parent = nullptr;
        // Whatever it was holding onto is on its way out, so the connector
        // voxels on both sides go back to being ordinary voxels
        child->attachment.reset();
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

    // An attachment is a parent link plus a pair of connector voxels, so a
    // grid moved elsewhere in the tree is no longer attached to what it was.
    // attach_to() clears this itself, so this only catches a plain reparent.
    if (attachment.has_value()) {
        TraceLog(LOG_INFO, "VOXELGRID: reparented an attached grid, dropping the attachment");
        attachment.reset();
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

// --- Voxels ---

Vector3 VoxelGrid::voxel_centre_local(const Int3 grid_pos) {
    // Grid space is the mesher's: X is grid x, Y is grid z (up), Z is grid y,
    // and a voxel spans one unit from its minimum corner
    return Vector3{
        static_cast<float>(grid_pos.x) + 0.5f,
        static_cast<float>(grid_pos.z) + 0.5f,
        static_cast<float>(grid_pos.y) + 0.5f,
    };
}

bool VoxelGrid::is_solid(const Int3 grid_pos) {
    // The bounds check comes first: VoxelMap::get_voxel wraps a coordinate
    // into a chunk rather than refusing it, so it would answer for the wrong
    // voxel outside the map
    if (!in_bounds(grid_pos)) return false;
    const VoxelID* voxel = get_voxel(grid_pos);
    return voxel != nullptr && *voxel != 0;
}

bool VoxelGrid::set_voxel(const Int3 grid_pos, const VoxelID id) {
    if (id == 0 && is_connector_voxel(grid_pos)) {
        TraceLog(LOG_INFO, "VOXELGRID: %d,%d,%d holds an attachment together, detach before clearing it",
                 grid_pos.x, grid_pos.y, grid_pos.z);
        return false;
    }
    return write_voxel(grid_pos, id);
}

// --- Attachment ---

bool VoxelGrid::attach_to(VoxelGrid* anchor, const Int3 anchor_voxel,
                          const Int3 local_voxel, const bool snap) {
    if (anchor == nullptr) {
        TraceLog(LOG_WARNING, "VOXELGRID: attach_to needs an anchor, detach() is what comes off one");
        return false;
    }

    // The same rule as set_parent(): a grid that ends up its own ancestor
    // sends get_world_transform() up a chain with no root. Checked here as
    // well so that a refused attachment leaves the one already in place alone.
    if (anchor == this || is_ancestor_of(anchor)) {
        TraceLog(LOG_WARNING, "VOXELGRID: refused an anchor that is already below this grid");
        return false;
    }

    // A connector has to be a voxel that is actually there: air would leave
    // the two grids held together by nothing, and a coordinate outside the
    // grid could never be cleared anyway
    if (!anchor->is_solid(anchor_voxel)) {
        TraceLog(LOG_WARNING, "VOXELGRID: no voxel to attach to at %d,%d,%d in the anchor",
                 anchor_voxel.x, anchor_voxel.y, anchor_voxel.z);
        return false;
    }
    if (!is_solid(local_voxel)) {
        TraceLog(LOG_WARNING, "VOXELGRID: no voxel to attach with at %d,%d,%d",
                 local_voxel.x, local_voxel.y, local_voxel.z);
        return false;
    }

    // Dropped before the reparent, so that moving from one anchor to another
    // does not look like the plain reparent set_parent() warns about
    attachment.reset();
    if (!set_parent(anchor)) return false;

    attachment = Attachment{anchor, anchor_voxel, local_voxel};
    if (snap) snap_to_anchor();
    return true;
}

bool VoxelGrid::detach() {
    if (!attachment.has_value()) return false;

    // Up one step rather than out of the tree: the grid takes the place its
    // anchor had, so a wheel taken off a car that is itself attached to
    // something stays with that something
    VoxelGrid* const anchor = attachment->anchor;
    VoxelGrid* const new_parent = anchor != nullptr ? anchor->get_parent() : nullptr;

    const Transform world = get_world_transform();

    attachment.reset();
    set_parent(new_parent);

    // Coming off something does not move it, so the local transform is redone
    // against whatever it hangs off now
    transform = new_parent != nullptr
        ? transform_relative_to(world, new_parent->get_world_transform())
        : world;
    return true;
}

bool VoxelGrid::is_connector_voxel(const Int3 grid_pos) const {
    if (attachment.has_value() && attachment->local_voxel == grid_pos) return true;

    // What is attached to this grid is read off the children rather than
    // stored twice, so the two can never disagree
    for (const VoxelGrid* child : children) {
        if (child == nullptr || !child->attachment.has_value()) continue;
        if (child->attachment->anchor == this && child->attachment->anchor_voxel == grid_pos)
            return true;
    }
    return false;
}

bool VoxelGrid::snap_to_anchor() {
    if (!attachment.has_value() || attachment->anchor == nullptr) return false;

    // Both connectors are worked out in the anchor's space, which is where
    // this grid's local transform puts a point of this grid
    const Vector3 anchor_point = voxel_centre_local(attachment->anchor_voxel);
    const Vector3 local_point = voxel_centre_local(attachment->local_voxel);

    // Only the translation moves: the rotation and scale are the grid's own,
    // and are how an attached grid turns on its anchor. apply_transform_trans
    // scales, rotates and then translates, so the translation wanted is
    // whatever is left over once the first two have been done.
    Transform turn_only = transform;
    turn_only.translation = Vector3{0.0f, 0.0f, 0.0f};
    const Vector3 turned = apply_transform_trans(local_point, turn_only);

    transform.translation = Vector3Subtract(anchor_point, turned);
    return true;
}
