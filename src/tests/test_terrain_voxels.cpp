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
    const BlockDetail plain;
    for (int z = 0; z < BLOCK_VOXELS; ++z) {
        for (int y = 0; y < BLOCK_VOXELS; ++y) {
            for (int x = 0; x < BLOCK_VOXELS; ++x) {
                const Int3 at{x, y, z};
                REQUIRE(block_voxel(sim::BlockType::Air, plain, at) == 0);
                REQUIRE(block_voxel(sim::BlockType::Stone, plain, at) == STONE_VOXEL);
                REQUIRE(block_voxel(sim::BlockType::Dirt, plain, at) == DIRT_VOXEL);
                // Dirt with one layer of green on top
                const VoxelID grass = block_voxel(sim::BlockType::Grass, plain, at);
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
                const BlockDetail detail = block_detail(terrain, x / BLOCK_VOXELS, y / BLOCK_VOXELS,
                                                        z / BLOCK_VOXELS);
                REQUIRE(*map.get_voxel(at) == block_voxel(block, detail, in_block));
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

namespace {

/**
 * A 3 by 3 floor of grass one block deep, with a bump in the middle: stone
 * under grass, two blocks tall.
 */
sim::Terrain bump_terrain() {
    sim::Terrain terrain(3, 3, 3);
    for (int x = 0; x < 3; ++x)
        for (int y = 0; y < 3; ++y)
            terrain.set(x, y, 0, sim::BlockType::Grass);
    terrain.set(1, 1, 0, sim::BlockType::Stone);
    terrain.set(1, 1, 1, sim::BlockType::Grass);
    return terrain;
}

constexpr uint8_t ALL_SIDES = SIDE_X_POS | SIDE_X_NEG | SIDE_Y_POS | SIDE_Y_NEG;

} // namespace

TEST_CASE("which blocks get lowered edges and which get trims", "[terrain][detail]") {
    const sim::Terrain terrain = bump_terrain();

    // The top of the bump has air all round it
    REQUIRE(block_detail(terrain, 1, 1, 1) == BlockDetail{ALL_SIDES, 0, sim::BlockType::Air});
    // Under it is buried: no open top, so no edge to lower
    REQUIRE(block_detail(terrain, 1, 1, 0) == BlockDetail{});
    // A floor block on the -X edge of the map, next to the bump: only the side
    // off the map is open
    REQUIRE(block_detail(terrain, 0, 1, 0).lowered == SIDE_X_NEG);
    // A corner of the floor: open on its two sides off the map
    REQUIRE(block_detail(terrain, 0, 0, 0).lowered == (SIDE_X_NEG | SIDE_Y_NEG));

    // The air beside the bump, on the floor: a trim against it, of grass,
    // with the floor's own lowered edges noted
    REQUIRE(block_detail(terrain, 0, 1, 1) == BlockDetail{0, SIDE_X_POS, sim::BlockType::Grass, SIDE_X_NEG});
    REQUIRE(block_detail(terrain, 1, 2, 1) == BlockDetail{0, SIDE_Y_NEG, sim::BlockType::Grass, SIDE_Y_POS});
    // Only diagonal to the bump: nothing to lean on
    REQUIRE(block_detail(terrain, 0, 0, 1) == BlockDetail{});
    // Above the bump: a floor but no wall
    REQUIRE(block_detail(terrain, 1, 1, 2) == BlockDetail{});

    // A pit between two walls and a floor gets a trim on both sides
    sim::Terrain pit(3, 1, 2);
    for (int x = 0; x < 3; ++x) pit.set(x, 0, 0, sim::BlockType::Dirt);
    pit.set(0, 0, 1, sim::BlockType::Stone);
    pit.set(2, 0, 1, sim::BlockType::Stone);
    // The floor is at the map's edge on both y sides, so its edges there are
    // lowered, and the trims stop short over them
    REQUIRE(block_detail(pit, 1, 0, 1) == BlockDetail{0, SIDE_X_POS | SIDE_X_NEG, sim::BlockType::Dirt,
                                                      static_cast<uint8_t>(SIDE_Y_POS | SIDE_Y_NEG)});
}

TEST_CASE("a lowered edge takes the top row off that side", "[terrain][detail]") {
    BlockDetail detail;
    detail.lowered = SIDE_X_POS;
    for (int y = 0; y < BLOCK_VOXELS; ++y) {
        REQUIRE(block_voxel(sim::BlockType::Stone, detail, Int3{3, y, 3}) == 0);
        REQUIRE(block_voxel(sim::BlockType::Stone, detail, Int3{3, y, 2}) == STONE_VOXEL);
        REQUIRE(block_voxel(sim::BlockType::Stone, detail, Int3{2, y, 3}) == STONE_VOXEL);
        REQUIRE(block_voxel(sim::BlockType::Stone, detail, Int3{0, y, 3}) == STONE_VOXEL);
    }

    // Two at once meet at the corner, and the middle of the top stays
    detail.lowered = SIDE_X_POS | SIDE_Y_POS;
    REQUIRE(block_voxel(sim::BlockType::Dirt, detail, Int3{3, 3, 3}) == 0);
    REQUIRE(block_voxel(sim::BlockType::Dirt, detail, Int3{1, 3, 3}) == 0);
    REQUIRE(block_voxel(sim::BlockType::Dirt, detail, Int3{3, 0, 3}) == 0);
    REQUIRE(block_voxel(sim::BlockType::Dirt, detail, Int3{2, 2, 3}) == DIRT_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Dirt, detail, Int3{0, 0, 3}) == DIRT_VOXEL);

    // All four leave a 2 by 2 top
    detail.lowered = ALL_SIDES;
    int left = 0;
    for (int x = 0; x < BLOCK_VOXELS; ++x)
        for (int y = 0; y < BLOCK_VOXELS; ++y)
            if (block_voxel(sim::BlockType::Stone, detail, Int3{x, y, 3}) != 0) ++left;
    REQUIRE(left == 4);
}

TEST_CASE("grass stays green on a lowered edge", "[terrain][detail]") {
    BlockDetail detail;
    detail.lowered = SIDE_X_NEG | SIDE_Y_NEG;
    // The edge: gone on top, green one below, dirt under that
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{0, 2, 3}) == 0);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{0, 2, 2}) == GRASS_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{0, 2, 1}) == DIRT_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{2, 0, 2}) == GRASS_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{0, 0, 2}) == GRASS_VOXEL);
    // The rest of the top is green where it always was, with dirt under it
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{2, 2, 3}) == GRASS_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{3, 3, 3}) == GRASS_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{2, 2, 2}) == DIRT_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Grass, detail, Int3{3, 3, 2}) == DIRT_VOXEL);
}

TEST_CASE("a trim is a row along the bottom, made of the floor", "[terrain][detail]") {
    BlockDetail detail;
    detail.trims = SIDE_X_POS;
    detail.trim_type = sim::BlockType::Grass;
    for (int y = 0; y < BLOCK_VOXELS; ++y) {
        REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, y, 0}) == GRASS_VOXEL);
        REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, y, 1}) == 0);
        REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{2, y, 0}) == 0);
        REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{0, y, 0}) == 0);
    }

    detail.trim_type = sim::BlockType::Stone;
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 1, 0}) == STONE_VOXEL);
    detail.trim_type = sim::BlockType::Dirt;
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 1, 0}) == DIRT_VOXEL);

    // Two at once, each along its own side
    detail.trims = SIDE_X_NEG | SIDE_Y_POS;
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{0, 1, 0}) == DIRT_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{2, 3, 0}) == DIRT_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{0, 3, 0}) == DIRT_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{1, 1, 0}) == 0);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 0, 0}) == 0);
}

TEST_CASE("a trim stops where the floor's edge is lowered under it", "[terrain][detail]") {
    // A floor block with a wall on +X and drops on -Y and +Y: the trim along
    // the wall runs over both of the floor's lowered edges at its ends
    sim::Terrain terrain(2, 3, 2);
    terrain.set(0, 1, 0, sim::BlockType::Grass);
    terrain.set(1, 1, 0, sim::BlockType::Stone);
    terrain.set(1, 1, 1, sim::BlockType::Grass);

    const BlockDetail detail = block_detail(terrain, 0, 1, 1);
    REQUIRE(detail.trims == SIDE_X_POS);
    REQUIRE((detail.floor_lowered & (SIDE_Y_POS | SIDE_Y_NEG)) == (SIDE_Y_POS | SIDE_Y_NEG));

    // The middle two are there, the two ends over the notches are not
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 1, 0}) == GRASS_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 2, 0}) == GRASS_VOXEL);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 0, 0}) == 0);
    REQUIRE(block_voxel(sim::BlockType::Air, detail, Int3{3, 3, 0}) == 0);

    // And so in the map, every trim voxel has something under it
    VoxelMap map(nullptr, 8, 12, 8);
    REQUIRE(build_terrain_voxels(map, terrain));
    for (int y = 0; y < 12; ++y) {
        for (int x = 0; x < 8; ++x) {
            const Int3 trim_layer{x, y, 4};
            if (*map.get_voxel(trim_layer) == 0) continue;
            // Either part of the wall block, or a trim standing on the floor
            const bool wall = x >= 4;
            REQUIRE((wall || *map.get_voxel(Int3{x, y, 3}) != 0));
        }
    }
}

TEST_CASE("the detail is drawn into the map", "[terrain][detail]") {
    const sim::Terrain terrain = bump_terrain();
    VoxelMap map(nullptr, 12, 12, 12);
    REQUIRE(build_terrain_voxels(map, terrain));

    // The bump is block (1, 1, 1), voxels 4 to 7 on every axis. Its -X top
    // edge is lowered, and the green comes down with it.
    REQUIRE(*map.get_voxel(Int3{4, 5, 7}) == 0);
    REQUIRE(*map.get_voxel(Int3{4, 5, 6}) == GRASS_VOXEL);
    REQUIRE(*map.get_voxel(Int3{5, 5, 7}) == GRASS_VOXEL);
    REQUIRE(*map.get_voxel(Int3{5, 5, 6}) == DIRT_VOXEL);

    // The air block beside it, (0, 1, 1), has a grass trim on its +X side,
    // on top of the floor and against the bump
    for (int y = 4; y < 8; ++y) REQUIRE(*map.get_voxel(Int3{3, y, 4}) == GRASS_VOXEL);
    REQUIRE(*map.get_voxel(Int3{3, 5, 5}) == 0);
    REQUIRE(*map.get_voxel(Int3{2, 5, 4}) == 0);

    // Diagonal to the bump, (0, 0, 1), there is nothing
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            REQUIRE(*map.get_voxel(Int3{x, y, 4}) == 0);
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
