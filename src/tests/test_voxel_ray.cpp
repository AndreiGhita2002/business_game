//
// Created by Claude on 20.09.2026.
//

#include "TestHelpers.hpp"

#include "game/Picking.hpp"
#include "sim/Rng.hpp"

#include <cfloat>
#include <cmath>

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

namespace {

/**
 * A small world for the marches: hills (a column of solid up to a height that
 * rolls across it) with the odd floating voxel, its size not a whole number of
 * coarse cells on any axis, so the last cells are cut short.
 */
struct MarchWorld {
    Int3 size{30, 22, 18};
    std::vector<uint8_t> voxels;

    MarchWorld() {
        voxels.assign(static_cast<size_t>(size.x) * size.y * size.z, 0);
        sim::Rng rng(7);
        for (int y = 0; y < size.y; ++y) {
            for (int x = 0; x < size.x; ++x) {
                const int height = 3 + (x * 7 + y * 3) % 6 + ((x / 5 + y / 4) % 2) * 3;
                for (int z = 0; z < height; ++z) voxels[index(x, y, z)] = 1;
            }
        }
        for (int i = 0; i < 40; ++i) {
            voxels[index(static_cast<int>(rng.next_below(size.x)), static_cast<int>(rng.next_below(size.y)),
                         12 + static_cast<int>(rng.next_below(size.z - 12)))] = 1;
        }
    }

    size_t index(const int x, const int y, const int z) const {
        return static_cast<size_t>(x) + static_cast<size_t>(y) * size.x + static_cast<size_t>(z) * size.x * size.y;
    }
    bool solid(const Int3 v) const { return voxels[index(v.x, v.y, v.z)] != 0; }
    // A cell is solid when any voxel of it inside the world is
    bool cell_solid(const Int3 c) const {
        for (int z = c.z * WORLD_COARSE; z < std::min((c.z + 1) * WORLD_COARSE, size.z); ++z)
            for (int y = c.y * WORLD_COARSE; y < std::min((c.y + 1) * WORLD_COARSE, size.y); ++y)
                for (int x = c.x * WORLD_COARSE; x < std::min((c.x + 1) * WORLD_COARSE, size.x); ++x)
                    if (solid(Int3{x, y, z})) return true;
        return false;
    }
};

float unit(sim::Rng& rng) { return static_cast<float>(rng.next_below(1000000)) / 1000000.0f; }

} // namespace

TEST_CASE("the coarse walk gives the same answer as the voxel walk", "[voxelray][coarse]") {
    const MarchWorld world;
    const VolumeSolid fine = [&world](const Int3 v) { return world.solid(v); };
    const VolumeSolid coarse = [&world](const Int3 c) { return world.cell_solid(c); };
    constexpr int UNLIMITED = 100000;

    sim::Rng rng(1234);
    int blocked = 0;
    long fine_steps = 0;
    long coarse_steps = 0;
    for (int i = 0; i < 4000; ++i) {
        // Starting inside the world or a little way outside it, going any way,
        // with every fourth ray on an axis or a diagonal
        const Vector3 origin = {
            -4.0f + unit(rng) * (static_cast<float>(world.size.x) + 8.0f),
            -4.0f + unit(rng) * (static_cast<float>(world.size.y) + 8.0f),
            -4.0f + unit(rng) * (static_cast<float>(world.size.z) + 8.0f),
        };
        Vector3 dir = {unit(rng) * 2.0f - 1.0f, unit(rng) * 2.0f - 1.0f, unit(rng) * 2.0f - 1.0f};
        if (i % 4 == 0) dir = Vector3{static_cast<float>(i % 3) - 1.0f, 0.0f, i % 8 == 0 ? 1.0f : -1.0f};
        if (Vector3Length(dir) < 1e-3f) continue;
        dir = Vector3Normalize(dir);

        int a = 0, b = 0;
        const bool by_voxel = volume_march(fine, world.size, origin, dir, UNLIMITED, &a);
        const bool by_cell = volume_march_coarse(fine, coarse, world.size, origin, dir, UNLIMITED, &b);
        INFO("ray " << i << " from " << origin.x << ", " << origin.y << ", " << origin.z
             << " along " << dir.x << ", " << dir.y << ", " << dir.z);
        REQUIRE(by_cell == by_voxel);
        blocked += by_voxel;
        fine_steps += a;
        coarse_steps += b;
    }

    // Both answers come up, and skipping empty cells reads fewer of them
    REQUIRE(blocked > 500);
    REQUIRE(blocked < 3500);
    REQUIRE(coarse_steps < fine_steps);
}

TEST_CASE("the voxel walk agrees with the grid walk from inside", "[voxelray][coarse]") {
    // volume_march() is the shader's walk; voxel_ray_blocked() is the older
    // twin, which has no box to enter. Started inside the grid they match.
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    sim::Rng rng(99);
    for (int i = 0; i < 300; ++i) {
        grid.set_voxel(Int3{static_cast<int>(rng.next_below(16)), static_cast<int>(rng.next_below(16)),
                            static_cast<int>(rng.next_below(16))}, 1);
    }
    const VolumeSolid solid = [&grid](const Int3 v) { return grid.is_solid(v); };

    for (int i = 0; i < 1000; ++i) {
        const Vector3 origin = {unit(rng) * 16.0f, unit(rng) * 16.0f, unit(rng) * 16.0f};
        const Vector3 dir = Vector3Normalize(Vector3{unit(rng) - 0.5f, unit(rng) - 0.5f, unit(rng) - 0.5f});
        REQUIRE(volume_march(solid, Int3{16, 16, 16}, origin, dir, 1000, nullptr) ==
                voxel_ray_blocked(&grid, origin, dir, 1000));
    }
}

TEST_CASE("the coarse walk stops, lit, when its steps run out", "[voxelray][coarse]") {
    const MarchWorld world;
    const VolumeSolid fine = [&world](const Int3 v) { return world.solid(v); };
    const VolumeSolid coarse = [&world](const Int3 c) { return world.cell_solid(c); };
    // Straight down from the top into the hills: blocked with room to spare,
    // lit with no steps at all
    const Vector3 origin{10.5f, 10.5f, 17.5f};
    const Vector3 down{0.0f, 0.0f, -1.0f};
    REQUIRE(volume_march_coarse(fine, coarse, world.size, origin, down, 256, nullptr));
    REQUIRE_FALSE(volume_march_coarse(fine, coarse, world.size, origin, down, 0, nullptr));
}

// --- Picking: the walk through a grid's voxels ---

TEST_CASE("a pick finds the voxel, the face it came in through, and how far", "[voxelray][pick]") {
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    grid.set_voxel(Int3{3, 4, 5}, 1);
    GridRayHit hit{};

    SECTION("from above, starting outside the grid") {
        REQUIRE(grid_ray_cast(&grid, Vector3{3.5f, 4.5f, 20.0f}, Vector3{0, 0, -1}, FLT_MAX, &hit));
        REQUIRE(hit.voxel == Int3{3, 4, 5});
        REQUIRE(hit.normal == Int3{0, 0, 1});
        REQUIRE(hit.t == Catch::Approx(14.0f));   // down to the voxel's top, at 6
    }
    SECTION("from the side, inside the grid") {
        REQUIRE(grid_ray_cast(&grid, Vector3{0.5f, 4.5f, 5.5f}, Vector3{1, 0, 0}, FLT_MAX, &hit));
        REQUIRE(hit.voxel == Int3{3, 4, 5});
        REQUIRE(hit.normal == Int3{-1, 0, 0});
        REQUIRE(hit.t == Catch::Approx(2.5f));
    }
    SECTION("a direction that is not a unit keeps its own t") {
        REQUIRE(grid_ray_cast(&grid, Vector3{0.5f, 4.5f, 5.5f}, Vector3{2, 0, 0}, FLT_MAX, &hit));
        REQUIRE(hit.t == Catch::Approx(1.25f));
    }
    SECTION("past the furthest it may look, or off to one side, nothing") {
        REQUIRE_FALSE(grid_ray_cast(&grid, Vector3{3.5f, 4.5f, 20.0f}, Vector3{0, 0, -1}, 10.0f, &hit));
        REQUIRE_FALSE(grid_ray_cast(&grid, Vector3{8.5f, 4.5f, 20.0f}, Vector3{0, 0, -1}, FLT_MAX, &hit));
        REQUIRE_FALSE(grid_ray_cast(&grid, Vector3{3.5f, 4.5f, 20.0f}, Vector3{0, 0, 1}, FLT_MAX, &hit));
    }
    SECTION("starting inside a solid voxel hits it at once") {
        REQUIRE(grid_ray_cast(&grid, Vector3{3.5f, 4.5f, 5.5f}, Vector3{0, 0, -1}, FLT_MAX, &hit));
        REQUIRE(hit.voxel == Int3{3, 4, 5});
        REQUIRE(hit.t == 0.0f);
        REQUIRE(hit.normal == Int3{0, 0, 1});
    }
}

TEST_CASE("a pick walks a sparse world map", "[voxelray][pick]") {
    // A map far bigger than its one chunk of ground, picked from high above at
    // a slant, the way the camera looks at it
    VoxelMap map(nullptr, 512, 512, 64);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) REQUIRE(map.set_voxel(Int3{200 + x, 300 + y, 2}, 1));

    const Vector3 target{208.5f, 308.5f, 3.0f};
    const Vector3 from{150.0f, 250.0f, 120.0f};
    GridRayHit hit{};
    REQUIRE(grid_ray_cast(&map, from, Vector3Normalize(Vector3Subtract(target, from)), FLT_MAX, &hit));
    REQUIRE(hit.voxel == Int3{208, 308, 2});
    REQUIRE(hit.normal == Int3{0, 0, 1});
}

TEST_CASE("a pick and the shadow walk agree on what a ray meets", "[voxelray][pick]") {
    const auto palette = make_palette();
    SingleChunkGrid grid(nullptr, palette);
    sim::Rng rng(5);
    for (int i = 0; i < 200; ++i) {
        grid.set_voxel(Int3{static_cast<int>(rng.next_below(16)), static_cast<int>(rng.next_below(16)),
                            static_cast<int>(rng.next_below(16))}, 1);
    }
    const VolumeSolid solid = [&grid](const Int3 v) { return grid.is_solid(v); };

    for (int i = 0; i < 2000; ++i) {
        const Vector3 origin = {-6.0f + unit(rng) * 28.0f, -6.0f + unit(rng) * 28.0f, -6.0f + unit(rng) * 28.0f};
        const Vector3 dir = Vector3Normalize(Vector3{unit(rng) - 0.5f, unit(rng) - 0.5f, unit(rng) - 0.5f});
        GridRayHit hit{};
        const bool picked = grid_ray_cast(&grid, origin, dir, FLT_MAX, &hit);
        REQUIRE(picked == volume_march(solid, Int3{16, 16, 16}, origin, dir, 100000, nullptr));
        if (!picked) continue;

        // What it found is solid, and the point it reports is on the face it
        // names: half a voxel back out through that face is the cell in front
        REQUIRE(grid.is_solid(hit.voxel));
        const Vector3 point = Vector3Add(origin, Vector3Scale(dir, hit.t));
        const Vector3 in_front = {point.x + 0.5f * static_cast<float>(hit.normal.x),
                                  point.y + 0.5f * static_cast<float>(hit.normal.y),
                                  point.z + 0.5f * static_cast<float>(hit.normal.z)};
        const Vector3 behind = {point.x - 0.5f * static_cast<float>(hit.normal.x),
                                point.y - 0.5f * static_cast<float>(hit.normal.y),
                                point.z - 0.5f * static_cast<float>(hit.normal.z)};
        // Starting inside the voxel there is no face in front to check
        if (hit.t > 0.0f) {
            REQUIRE(Int3{static_cast<int>(std::floor(behind.x)), static_cast<int>(std::floor(behind.y)),
                         static_cast<int>(std::floor(behind.z))} == hit.voxel);
            REQUIRE_FALSE(grid.is_solid(Int3{static_cast<int>(std::floor(in_front.x)),
                                             static_cast<int>(std::floor(in_front.y)),
                                             static_cast<int>(std::floor(in_front.z))}));
        }
    }
}
