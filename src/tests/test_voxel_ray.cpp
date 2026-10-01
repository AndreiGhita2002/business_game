//
// Created by Claude on 20.09.2026.
//

#include "TestHelpers.hpp"

#include "game/Picking.hpp"

/**
 * The voxel walk a shadow ray does.
 *
 * voxel_ray_blocked() is the C++ twin of march_volume() in
 * resources/shaders/lighting.fs, which is what these cases are really about:
 * the shader cannot be tested, and every case here is one that would show up in
 * the game as a stripe of wrong shadow rather than as an obvious break.
 */

using test::make_palette;

TEST_CASE("a ray through air leaves the grid", "[voxelray]") {
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);

    REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 0.5f}, Vector3{0.0f, 0.0f, 1.0f}));
}

TEST_CASE("a solid voxel blocks the ray", "[voxelray]") {
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    grid.set_voxel(Int3{1, 1, 5}, 1);

    SECTION("straight up into it") {
        REQUIRE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 0.5f}, Vector3{0.0f, 0.0f, 1.0f}));
    }
    SECTION("a column over from it, so the ray misses") {
        REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{0.5f, 0.5f, 0.5f}, Vector3{0.0f, 0.0f, 1.0f}));
    }
    SECTION("from above, going the other way") {
        REQUIRE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 9.5f}, Vector3{0.0f, 0.0f, -1.0f}));
    }
    SECTION("away from it, so the ray leaves through the top") {
        REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 9.5f}, Vector3{0.0f, 0.0f, 1.0f}));
    }
}

TEST_CASE("a ray starting exactly on a face", "[voxelray]") {
    // Where a shadow ray actually starts: on the surface of a voxel, which is
    // the boundary between two of them. Which one it belongs to decides whether
    // a lit face reports itself in shadow.
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    grid.set_voxel(Int3{1, 1, 4}, 1);

    SECTION("leaving the solid voxel is not blocked by it") {
        REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 5.0f}, Vector3{0.0f, 0.0f, 1.0f}));
    }
    SECTION("going back into it is") {
        REQUIRE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 5.0f}, Vector3{0.0f, 0.0f, -1.0f}));
    }
}

TEST_CASE("a ray along the diagonal", "[voxelray]") {
    // The walk crosses two boundaries at once here, so a step that took both at
    // the same time would skip the voxel on the corner.
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    grid.set_voxel(Int3{2, 2, 2}, 1);

    REQUIRE(voxel_ray_blocked(&grid, Vector3{0.5f, 0.5f, 0.5f}, Vector3{1.0f, 1.0f, 1.0f}));
}

TEST_CASE("a ray parallel to an axis does not drift", "[voxelray]") {
    // A zero in the direction is the division the walk has to survive. The ray
    // runs the length of the grid one column over from a wall of voxels and
    // must not wander into it.
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    for (int x = 0; x < CHUNK_SIZE; ++x) grid.set_voxel(Int3{x, 2, 3}, 1);

    REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{0.5f, 1.5f, 3.5f}, Vector3{1.0f, 0.0f, 0.0f}));
    REQUIRE(voxel_ray_blocked(&grid, Vector3{0.5f, 2.5f, 3.5f}, Vector3{1.0f, 0.0f, 0.0f}));
}

TEST_CASE("the step cap gives up rather than running on", "[voxelray]") {
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    grid.set_voxel(Int3{1, 1, 12}, 1);

    // Reaching it takes a dozen steps, so a cap below that answers lit
    REQUIRE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 0.5f}, Vector3{0.0f, 0.0f, 1.0f}, 256));
    REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{1.5f, 1.5f, 0.5f}, Vector3{0.0f, 0.0f, 1.0f}, 4));
}

TEST_CASE("a ray outside the grid is not blocked", "[voxelray]") {
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    grid.set_voxel(Int3{1, 1, 1}, 1);

    // Leaves immediately: nothing carries it back to the grid, which is the one
    // thing the shader does differently
    REQUIRE_FALSE(voxel_ray_blocked(&grid, Vector3{-4.5f, 1.5f, 1.5f}, Vector3{1.0f, 0.0f, 0.0f}));
    REQUIRE_FALSE(voxel_ray_blocked(nullptr, Vector3{0.5f, 0.5f, 0.5f}, Vector3{0.0f, 0.0f, 1.0f}));
}

/**
 * The boxes that decide which grids a draw call is traced against. Getting
 * these wrong drops a shadow that should be there, or traces volumes that could
 * never reach the model, so both are worth pinning.
 */

TEST_CASE("the box a grid fills in the world", "[voxelbounds]") {
    SECTION("at the origin it is the cube itself") {
        const BoundingBox box = voxel_box_bounds(MatrixIdentity(), CHUNK_SIZE);
        REQUIRE_VEC3_EQ(box.min, (Vector3{0.0f, 0.0f, 0.0f}));
        REQUIRE_VEC3_EQ(box.max, (Vector3{16.0f, 16.0f, 16.0f}));
    }

    SECTION("a moved grid moves its box") {
        const BoundingBox box = voxel_box_bounds(MatrixTranslate(10.0f, -4.0f, 2.0f), CHUNK_SIZE);
        REQUIRE_VEC3_EQ(box.min, (Vector3{10.0f, -4.0f, 2.0f}));
        REQUIRE_VEC3_EQ(box.max, (Vector3{26.0f, 12.0f, 18.0f}));
    }

    SECTION("a turned grid gives the box its corners reach") {
        // Turned an eighth of a turn about the up axis, the cube's corners
        // swing out to its diagonal, while its height is untouched. Written as
        // widths so it holds whichever way raylib turns things.
        const BoundingBox box = voxel_box_bounds(MatrixRotateY(45.0f * DEG2RAD), CHUNK_SIZE);
        const float diagonal = 16.0f * sqrtf(2.0f);

        REQUIRE(box.max.x - box.min.x == Catch::Approx(diagonal).margin(test::EPS));
        REQUIRE(box.max.z - box.min.z == Catch::Approx(diagonal).margin(test::EPS));
        REQUIRE(box.min.y == Catch::Approx(0.0f).margin(test::EPS));
        REQUIRE(box.max.y == Catch::Approx(16.0f).margin(test::EPS));
    }
}

TEST_CASE("which grids could shadow a model", "[voxelbounds]") {
    const BoundingBox receiver = voxel_box_bounds(MatrixIdentity(), CHUNK_SIZE);
    const BoundingBox above = voxel_box_bounds(MatrixTranslate(0.0f, 40.0f, 0.0f), CHUNK_SIZE);
    const Vector3 straight_down{0.0f, -1.0f, 0.0f};

    SECTION("one overhead does, with the light coming down") {
        REQUIRE(box_casts_onto(above, receiver, straight_down, 64.0f));
    }
    SECTION("not if its shadow gives out before it arrives") {
        REQUIRE_FALSE(box_casts_onto(above, receiver, straight_down, 8.0f));
    }
    SECTION("not if it is off to one side") {
        const BoundingBox aside = voxel_box_bounds(MatrixTranslate(100.0f, 40.0f, 0.0f), CHUNK_SIZE);
        REQUIRE_FALSE(box_casts_onto(aside, receiver, straight_down, 64.0f));
    }
    SECTION("a grid always reaches itself, which is what shadows a vehicle with its own shape") {
        REQUIRE(box_casts_onto(receiver, receiver, straight_down, 64.0f));
    }
}
