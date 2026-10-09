//
// Created by Claude on 09.10.2026.
//

#include "TestHelpers.hpp"

#include <algorithm>

#include "PerlinNoise.hpp"
#include "entity/SimConvert.hpp"
#include "entity/TerrainVoxels.hpp"
#include "sim/Noise.hpp"

/**
 * The simulation's terrain made visible: blocks drawn as cubes of voxels, and
 * the simulation's noise checked against the library the terrain used to be
 * generated with, which only this side of the line may include.
 */

using Catch::Approx;

TEST_CASE("the simulation's noise is siv::PerlinNoise in fixed point", "[terrain][noise]") {
    constexpr uint32_t SEED = 123456;
    const siv::PerlinNoise reference{SEED};
    const sim::PerlinNoise noise(SEED);

    // The same shuffle of the same engine
    const auto& expected = reference.serialize();
    for (size_t i = 0; i < expected.size(); ++i) REQUIRE(noise.permutation()[i] == expected[i]);

    // And the same values, to within the rounding of a few Fixed multiplies.
    // Negative coordinates included, which wrap the lattice.
    for (int i = -30; i <= 30; ++i) {
        for (int j = -30; j <= 30; ++j) {
            const sim::Fixed x = sim::Fixed::from_ratio(i * 13, 17);
            const sim::Fixed y = sim::Fixed::from_ratio(j * 11, 19);
            const double want = reference.noise2D(to_float(x), to_float(y));
            REQUIRE(to_float(noise.noise2d(x, y)) == Approx(want).margin(1e-3));
        }
    }
}

TEST_CASE("each block type is drawn in its own voxels", "[terrain]") {
    for (int z = 0; z < BLOCK_VOXELS; ++z) {
        for (int y = 0; y < BLOCK_VOXELS; ++y) {
            for (int x = 0; x < BLOCK_VOXELS; ++x) {
                const Int3 at{x, y, z};
                REQUIRE(block_voxel(sim::BlockType::Air, at) == 0);
                REQUIRE(block_voxel(sim::BlockType::Stone, at) == STONE_VOXEL);
                REQUIRE(block_voxel(sim::BlockType::Dirt, at) == DIRT_VOXEL);
                // Dirt with one layer of green on top
                const VoxelID grass = block_voxel(sim::BlockType::Grass, at);
                REQUIRE(grass == (z == BLOCK_VOXELS - 1 ? GRASS_VOXEL : DIRT_VOXEL));
            }
        }
    }
}

TEST_CASE("the terrain's voxels are in the map's palette", "[terrain]") {
    const VoxelMap map(nullptr, 16, 16);
    REQUIRE(map.voxel_colours->count(STONE_VOXEL) == 1);
    REQUIRE(map.voxel_colours->count(DIRT_VOXEL) == 1);
    REQUIRE(map.voxel_colours->count(GRASS_VOXEL) == 1);
}

TEST_CASE("a terrain is drawn into the map a block to a cube of voxels", "[terrain]") {
    sim::Terrain terrain(3, 2, 2);
    terrain.set(0, 0, 0, sim::BlockType::Stone);
    terrain.set(0, 0, 1, sim::BlockType::Grass);
    terrain.set(2, 1, 0, sim::BlockType::Dirt);

    const Int3 size = terrain_voxel_size(terrain);
    REQUIRE(size.x == 3 * BLOCK_VOXELS);
    REQUIRE(size.y == 2 * BLOCK_VOXELS);
    REQUIRE(size.z == 2 * BLOCK_VOXELS);

    // Two chunks across, so the terrain is not all in one of them, and one
    // voxel left over from before that has to go
    VoxelMap map(nullptr, 32, 16);
    *map.get_voxel(Int3{20, 10, 3}) = 5;
    for (auto& [chunk_pos, dirty] : map.chunk_was_updated) dirty = false;
    for (auto& [chunk_pos, dirty] : map.chunk_volume_dirty) dirty = false;

    REQUIRE(build_terrain_voxels(map, terrain));

    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 32; ++x) {
                const Int3 at{x, y, z};
                const Int3 in_block{x % BLOCK_VOXELS, y % BLOCK_VOXELS, z % BLOCK_VOXELS};
                const sim::BlockType block = terrain.get(x / BLOCK_VOXELS, y / BLOCK_VOXELS, z / BLOCK_VOXELS);
                REQUIRE(*map.get_voxel(at) == block_voxel(block, in_block));
            }
        }
    }

    // Spot checks of the same thing: grass on top of the stone, green only
    // on its top layer
    REQUIRE(*map.get_voxel(Int3{1, 2, 3}) == STONE_VOXEL);
    REQUIRE(*map.get_voxel(Int3{1, 2, 4}) == DIRT_VOXEL);
    REQUIRE(*map.get_voxel(Int3{1, 2, 7}) == GRASS_VOXEL);
    REQUIRE(*map.get_voxel(Int3{1, 2, 8}) == 0);
    REQUIRE(*map.get_voxel(Int3{9, 5, 0}) == DIRT_VOXEL);
    REQUIRE(*map.get_voxel(Int3{20, 10, 3}) == 0);

    for (const auto& [chunk_pos, dirty] : map.chunk_was_updated) REQUIRE(dirty);
    for (const auto& [chunk_pos, dirty] : map.chunk_volume_dirty) REQUIRE(dirty);
}

TEST_CASE("a terrain bigger than the map is drawn as far as it fits", "[terrain]") {
    // Five blocks tall is 20 voxels, and this map is one chunk (16) tall
    sim::Terrain terrain(2, 2, 5);
    for (int z = 0; z < 5; ++z) terrain.set(0, 0, z, sim::BlockType::Stone);
    // And past the map's edge on x
    terrain.set(1, 0, 0, sim::BlockType::Dirt);

    VoxelMap map(nullptr, 6, 8);
    REQUIRE_FALSE(build_terrain_voxels(map, terrain));

    REQUIRE(*map.get_voxel(Int3{0, 0, 15}) == STONE_VOXEL);
    REQUIRE(*map.get_voxel(Int3{5, 0, 0}) == DIRT_VOXEL);
}

TEST_CASE("the game's map is 128 voxels on every side", "[terrain]") {
    // The water, the camera and the test routes were all laid out for a 128
    // voxel map, and it is 128 tall since the map could have more chunks than one
    const sim::Terrain terrain = sim::generate_terrain(sim::TerrainSettings{});
    const Int3 size = terrain_voxel_size(terrain);
    REQUIRE(size.x == 128);
    REQUIRE(size.y == 128);
    REQUIRE(size.z == 128);

    VoxelMap map(nullptr, size.x, size.y, size.z);
    REQUIRE(map.get_height() == 128);
    REQUIRE(map.get_chunk_count() == Int3{8, 8, 8});
    REQUIRE(build_terrain_voxels(map, terrain));

    // The shadow volume is cut off at the highest block there is
    int highest = 0;
    for (int x = 0; x < terrain.size_x(); ++x)
        for (int y = 0; y < terrain.size_y(); ++y)
            highest = std::max(highest, terrain.column_height(x, y));
    REQUIRE(map.solid_top() == highest * BLOCK_VOXELS);
}

TEST_CASE("a map several chunks tall", "[terrain]") {
    VoxelMap map(nullptr, 16, 16, 40);
    REQUIRE(map.get_height() == 40);
    // 40 is two chunks and a part
    REQUIRE(map.get_chunk_count() == Int3{1, 1, 3});
    REQUIRE(map.solid_top() == 0);

    REQUIRE(map.in_bounds(Int3{0, 0, 39}));
    REQUIRE_FALSE(map.in_bounds(Int3{0, 0, 40}));
    REQUIRE_FALSE(map.set_voxel(Int3{0, 0, 40}, 1));

    // A voxel in the upper chunk lands there and nowhere else
    for (auto& [chunk_pos, dirty] : map.chunk_was_updated) dirty = false;
    REQUIRE(map.set_voxel(Int3{3, 4, 35}, 2));
    REQUIRE(*map.get_voxel(Int3{3, 4, 35}) == 2);
    REQUIRE(*map.get_voxel(Int3{3, 4, 3}) == 0);
    REQUIRE(*map.get_voxel(Int3{3, 4, 19}) == 0);
    REQUIRE(map.solid_top() == 36);
    REQUIRE(map.chunk_was_updated[Int3{0, 0, 2}]);
    REQUIRE_FALSE(map.chunk_was_updated[Int3{0, 0, 0}]);

    // On a chunk's bottom layer, the chunk under it is remeshed too, as its
    // top faces and corners read across the border
    for (auto& [chunk_pos, dirty] : map.chunk_was_updated) dirty = false;
    REQUIRE(map.set_voxel(Int3{3, 4, 16}, 2));
    REQUIRE(map.chunk_was_updated[Int3{0, 0, 1}]);
    REQUIRE(map.chunk_was_updated[Int3{0, 0, 0}]);
    REQUIRE_FALSE(map.chunk_was_updated[Int3{0, 0, 2}]);
}
