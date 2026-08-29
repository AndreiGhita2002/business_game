//
// Created by Andrei Ghita on 29.08.2026.
//
// The grid hierarchy and the attachment system in voxel/VoxelGrid.cpp.
//
// Nothing here meshes or draws, so the grids are built with a null VoxelView
// and update_models() is never called.
//

#include <catch2/catch_test_macros.hpp>

// First, so that raylib.h is in before raymath.h
#include "tests/TestHelpers.hpp"

#include "game/Transform.hpp"

namespace {

Quaternion yaw(const float degrees) {
    return QuaternionFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, degrees * DEG2RAD);
}

/** A turn about X, for the cases where the order of two turns has to matter. */
Quaternion pitch(const float degrees) {
    return QuaternionFromAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, degrees * DEG2RAD);
}

/**
 * Where the grid's own connector voxel ends up in its parent's space. Attaching
 * is meant to land this on the anchor's connector voxel.
 */
Vector3 connector_in_parent_space(const VoxelGrid& grid, const Int3 local_voxel) {
    return apply_transform_trans(VoxelGrid::voxel_centre_local(local_voxel),
                                 grid.get_transform());
}

}

// --- The hierarchy on its own ---

TEST_CASE("a child's world transform folds in its parents", "[hierarchy]") {
    const auto palette = test::make_palette();
    const auto root = test::make_chunk_grid(palette);
    const auto middle = test::make_chunk_grid(palette);
    const auto leaf = test::make_chunk_grid(palette);

    root->set_transform(Transform{Vector3{10.0f, 0.0f, 0.0f}, QuaternionIdentity(),
                                  Vector3{1.0f, 1.0f, 1.0f}});
    middle->set_transform(Transform{Vector3{0.0f, 5.0f, 0.0f}, QuaternionIdentity(),
                                    Vector3{1.0f, 1.0f, 1.0f}});
    leaf->set_transform(Transform{Vector3{0.0f, 0.0f, 2.0f}, QuaternionIdentity(),
                                  Vector3{1.0f, 1.0f, 1.0f}});

    REQUIRE(root->add_child(middle.get()));
    REQUIRE(middle->add_child(leaf.get()));

    // The local transform is untouched by being parented
    REQUIRE_VEC3_EQ(leaf->get_transform().translation, (Vector3{0.0f, 0.0f, 2.0f}));
    REQUIRE_VEC3_EQ(leaf->get_world_transform().translation, (Vector3{10.0f, 5.0f, 2.0f}));
}

TEST_CASE("a grid refuses a parent that is already below it", "[hierarchy]") {
    const auto palette = test::make_palette();
    const auto parent = test::make_chunk_grid(palette);
    const auto child = test::make_chunk_grid(palette);

    REQUIRE(parent->add_child(child.get()));

    // Either of these would send get_world_transform() up a chain with no root
    REQUIRE_FALSE(parent->set_parent(child.get()));
    REQUIRE_FALSE(parent->set_parent(parent.get()));

    REQUIRE(parent->get_parent() == nullptr);
    REQUIRE(child->get_parent() == parent.get());
}

TEST_CASE("is_ancestor_of only looks downwards", "[hierarchy]") {
    const auto palette = test::make_palette();
    const auto root = test::make_chunk_grid(palette);
    const auto middle = test::make_chunk_grid(palette);
    const auto leaf = test::make_chunk_grid(palette);

    REQUIRE(root->add_child(middle.get()));
    REQUIRE(middle->add_child(leaf.get()));

    REQUIRE(root->is_ancestor_of(middle.get()));
    REQUIRE(root->is_ancestor_of(leaf.get()));
    REQUIRE(middle->is_ancestor_of(leaf.get()));

    REQUIRE_FALSE(leaf->is_ancestor_of(root.get()));
    REQUIRE_FALSE(root->is_ancestor_of(root.get()));
}

TEST_CASE("a destroyed parent leaves its children where they stood", "[hierarchy]") {
    const auto palette = test::make_palette();
    const auto child = test::make_chunk_grid(palette);
    Transform child_world{};

    {
        const auto parent = test::make_chunk_grid(palette);
        parent->set_transform(Transform{Vector3{4.0f, 0.0f, 0.0f}, yaw(90.0f),
                                        Vector3{1.0f, 1.0f, 1.0f}});
        child->set_transform(Transform{Vector3{1.0f, 0.0f, 0.0f}, pitch(60.0f),
                                       Vector3{1.0f, 1.0f, 1.0f}});
        REQUIRE(parent->add_child(child.get()));
        child_world = child->get_world_transform();
    }

    // The local transform has been rewritten to the world one it was standing at
    REQUIRE(child->get_parent() == nullptr);
    REQUIRE_TRANSFORM_EQ(child->get_world_transform(), child_world);
}

// --- What attach_to refuses ---

TEST_CASE("attach_to refuses an anchor it cannot use", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    SECTION("no anchor at all") {
        REQUIRE_FALSE(grid->attach_to(nullptr, Int3{2, 2, 2}, Int3{1, 1, 1}));
    }

    SECTION("itself") {
        REQUIRE_FALSE(grid->attach_to(grid.get(), Int3{1, 1, 1}, Int3{1, 1, 1}));
    }

    SECTION("a grid already below it") {
        REQUIRE(grid->add_child(anchor.get()));
        REQUIRE_FALSE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));
    }

    REQUIRE_FALSE(grid->is_attached());
}

TEST_CASE("attach_to needs a solid voxel on both sides", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    SECTION("the anchor's connector is air") {
        REQUIRE_FALSE(grid->attach_to(anchor.get(), Int3{5, 5, 5}, Int3{1, 1, 1}));
    }

    SECTION("the grid's own connector is air") {
        REQUIRE_FALSE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{7, 7, 7}));
    }

    SECTION("a connector outside the grid is not solid either") {
        REQUIRE_FALSE(grid->attach_to(anchor.get(), Int3{-1, 0, 0}, Int3{1, 1, 1}));
        REQUIRE_FALSE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{CHUNK_SIZE, 0, 0}));
    }

    REQUIRE_FALSE(grid->is_attached());
    REQUIRE(grid->get_parent() == nullptr);
}

TEST_CASE("a refused attachment leaves the one already in place alone", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));

    // Air on the anchor side, so this one is turned away
    REQUIRE_FALSE(grid->attach_to(anchor.get(), Int3{9, 9, 9}, Int3{1, 1, 1}));

    REQUIRE(grid->is_attached());
    REQUIRE(grid->get_attachment()->anchor_voxel == Int3{2, 2, 2});
}

// --- What a successful attachment does ---

TEST_CASE("attaching makes the grid a child of its anchor", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));

    REQUIRE(grid->is_attached());
    REQUIRE(grid->get_parent() == anchor.get());
    REQUIRE(anchor->get_children().size() == 1);
    REQUIRE(anchor->get_children()[0] == grid.get());

    const Attachment* attachment = grid->get_attachment();
    REQUIRE(attachment != nullptr);
    REQUIRE(attachment->anchor == anchor.get());
    REQUIRE(attachment->anchor_voxel == Int3{2, 2, 2});
    REQUIRE(attachment->local_voxel == Int3{1, 1, 1});
}

TEST_CASE("attaching puts the two connector voxels in the same place", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{4, 5, 6});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(anchor.get(), Int3{4, 5, 6}, Int3{1, 1, 1}));

    REQUIRE_VEC3_EQ(connector_in_parent_space(*grid, Int3{1, 1, 1}),
                    VoxelGrid::voxel_centre_local(Int3{4, 5, 6}));
}

TEST_CASE("attaching without snapping leaves the transform exactly as it was", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{4, 5, 6});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    const Transform before{Vector3{3.0f, 2.0f, 1.0f}, yaw(20.0f), Vector3{1.0f, 1.0f, 1.0f}};
    grid->set_transform(before);

    REQUIRE(grid->attach_to(anchor.get(), Int3{4, 5, 6}, Int3{1, 1, 1}, false));

    REQUIRE(grid->is_attached());
    REQUIRE_TRANSFORM_EQ(grid->get_transform(), before);
}

TEST_CASE("snap_to_anchor puts a turned grid back on its anchor", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{4, 5, 6});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(anchor.get(), Int3{4, 5, 6}, Int3{1, 1, 1}));

    // Turning about the grid's own origin carries the connector off the anchor
    Transform turned = grid->get_transform();
    turned.rotation = yaw(90.0f);
    grid->set_transform(turned);

    const Vector3 drifted = connector_in_parent_space(*grid, Int3{1, 1, 1});
    const Vector3 target = VoxelGrid::voxel_centre_local(Int3{4, 5, 6});
    REQUIRE(Vector3Distance(drifted, target) > test::EPS);

    REQUIRE(grid->snap_to_anchor());

    // Back on the anchor, and still turned
    REQUIRE_VEC3_EQ(connector_in_parent_space(*grid, Int3{1, 1, 1}), target);
    REQUIRE_QUAT_EQ(grid->get_transform().rotation, yaw(90.0f));
}

TEST_CASE("snap_to_anchor does nothing to a grid that is not attached", "[attachment]") {
    const auto palette = test::make_palette();
    const auto grid = test::make_chunk_grid(palette);

    REQUIRE_FALSE(grid->snap_to_anchor());
}

TEST_CASE("attaching to a second anchor moves the grid over", "[attachment]") {
    const auto palette = test::make_palette();
    const auto first = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto second = test::make_chunk_grid(palette, Int3{7, 7, 7});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(first.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));
    REQUIRE(grid->attach_to(second.get(), Int3{7, 7, 7}, Int3{1, 1, 1}));

    REQUIRE(grid->get_parent() == second.get());
    REQUIRE(grid->get_attachment()->anchor == second.get());
    REQUIRE(grid->get_attachment()->anchor_voxel == Int3{7, 7, 7});
    REQUIRE(first->get_children().empty());
}

// --- Connector voxels ---

TEST_CASE("a connector voxel cannot be cleared while the attachment stands", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    // An ordinary voxel on each side, to show the refusal is about the connector
    REQUIRE(anchor->set_voxel(Int3{5, 5, 5}, 2));
    REQUIRE(grid->set_voxel(Int3{3, 3, 3}, 2));

    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));

    SECTION("the grid's own connector") {
        REQUIRE(grid->is_connector_voxel(Int3{1, 1, 1}));
        REQUIRE_FALSE(grid->set_voxel(Int3{1, 1, 1}, 0));
        REQUIRE(grid->is_solid(Int3{1, 1, 1}));
    }

    SECTION("the anchor's connector, which it reads off its children") {
        REQUIRE(anchor->is_connector_voxel(Int3{2, 2, 2}));
        REQUIRE_FALSE(anchor->set_voxel(Int3{2, 2, 2}, 0));
        REQUIRE(anchor->is_solid(Int3{2, 2, 2}));
    }

    SECTION("painting a connector another colour is still fine") {
        REQUIRE(grid->set_voxel(Int3{1, 1, 1}, 3));
        REQUIRE(*grid->get_voxel(Int3{1, 1, 1}) == 3);
        REQUIRE(anchor->set_voxel(Int3{2, 2, 2}, 3));
        REQUIRE(*anchor->get_voxel(Int3{2, 2, 2}) == 3);
    }

    SECTION("an ordinary voxel clears as usual") {
        REQUIRE_FALSE(grid->is_connector_voxel(Int3{3, 3, 3}));
        REQUIRE(grid->set_voxel(Int3{3, 3, 3}, 0));
        REQUIRE_FALSE(grid->is_solid(Int3{3, 3, 3}));

        REQUIRE(anchor->set_voxel(Int3{5, 5, 5}, 0));
        REQUIRE_FALSE(anchor->is_solid(Int3{5, 5, 5}));
    }
}

TEST_CASE("a connector voxel clears again once the grid is detached", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));
    REQUIRE(grid->detach());

    REQUIRE_FALSE(grid->is_connector_voxel(Int3{1, 1, 1}));
    REQUIRE_FALSE(anchor->is_connector_voxel(Int3{2, 2, 2}));
    REQUIRE(grid->set_voxel(Int3{1, 1, 1}, 0));
    REQUIRE(anchor->set_voxel(Int3{2, 2, 2}, 0));
}

// --- Coming off an anchor ---

TEST_CASE("reparenting an attached grid drops the attachment", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto elsewhere = test::make_chunk_grid(palette);
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));
    REQUIRE(grid->set_parent(elsewhere.get()));

    // The anchor and the parent may never disagree, so the attachment goes
    REQUIRE_FALSE(grid->is_attached());
    REQUIRE(grid->get_attachment() == nullptr);
    REQUIRE(grid->get_parent() == elsewhere.get());
    REQUIRE_FALSE(anchor->is_connector_voxel(Int3{2, 2, 2}));
}

TEST_CASE("detach moves the grid up to the anchor's own parent", "[attachment]") {
    const auto palette = test::make_palette();
    const auto grandparent = test::make_chunk_grid(palette);
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    // Different axes, so that a world transform composed the wrong way round
    // would not come out right by accident
    grandparent->set_transform(Transform{Vector3{100.0f, 0.0f, 0.0f}, pitch(45.0f),
                                         Vector3{1.0f, 1.0f, 1.0f}});
    anchor->set_transform(Transform{Vector3{0.0f, 3.0f, 0.0f}, yaw(30.0f),
                                    Vector3{1.0f, 1.0f, 1.0f}});
    REQUIRE(grandparent->add_child(anchor.get()));
    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));

    const Transform world_before = grid->get_world_transform();

    REQUIRE(grid->detach());

    // Up one step, not out of the tree
    REQUIRE(grid->get_parent() == grandparent.get());
    REQUIRE_FALSE(grid->is_attached());
    // Coming off something does not move it
    REQUIRE_TRANSFORM_EQ(grid->get_world_transform(), world_before);
}

TEST_CASE("detach from an anchor with no parent leaves the grid at the root", "[attachment]") {
    const auto palette = test::make_palette();
    const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});

    anchor->set_transform(Transform{Vector3{7.0f, 1.0f, -3.0f}, yaw(15.0f),
                                    Vector3{1.0f, 1.0f, 1.0f}});
    REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));

    const Transform world_before = grid->get_world_transform();

    REQUIRE(grid->detach());

    REQUIRE(grid->get_parent() == nullptr);
    REQUIRE_TRANSFORM_EQ(grid->get_transform(), world_before);
}

TEST_CASE("detach on a grid that is not attached does nothing", "[attachment]") {
    const auto palette = test::make_palette();
    const auto parent = test::make_chunk_grid(palette);
    const auto grid = test::make_chunk_grid(palette);

    REQUIRE(parent->add_child(grid.get()));

    // Merely hanging off a parent is not an attachment
    REQUIRE_FALSE(grid->detach());
    REQUIRE(grid->get_parent() == parent.get());
}

TEST_CASE("a destroyed anchor detaches what was on it", "[attachment]") {
    const auto palette = test::make_palette();
    const auto grid = test::make_chunk_grid(palette, Int3{1, 1, 1});
    Transform world_before{};

    {
        const auto anchor = test::make_chunk_grid(palette, Int3{2, 2, 2});
        anchor->set_transform(Transform{Vector3{6.0f, 0.0f, 0.0f}, yaw(60.0f),
                                        Vector3{1.0f, 1.0f, 1.0f}});
        REQUIRE(grid->attach_to(anchor.get(), Int3{2, 2, 2}, Int3{1, 1, 1}));
        world_before = grid->get_world_transform();
    }

    REQUIRE_FALSE(grid->is_attached());
    REQUIRE(grid->get_parent() == nullptr);
    REQUIRE_TRANSFORM_EQ(grid->get_world_transform(), world_before);
    // The connector is an ordinary voxel again, so it can be cleared
    REQUIRE(grid->set_voxel(Int3{1, 1, 1}, 0));
}
