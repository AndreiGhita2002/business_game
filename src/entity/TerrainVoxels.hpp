//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_TERRAINVOXELS_HPP
#define BUSINESS_GAME_TERRAINVOXELS_HPP

#include "sim/Terrain.hpp"
#include "voxel/VoxelGrid.hpp"

class VoxelMap;

/**
 * The simulation's terrain made visible: every block drawn as a cube of
 * voxels in the VoxelMap. One direction only, like the rest of src/entity -
 * editing the map's voxels changes nothing in the simulation, and the next
 * build_terrain_voxels() puts them back.
 *
 * This is where a block gets more detail than the simulation gives it. A
 * block's voxels depend on its type, on where they sit inside it, and on its
 * four side neighbours and the blocks above and below (block_detail()), which
 * is what rounds the block grid off a little without hiding it:
 *
 *   - Lowered edges. A solid block with air above it loses its top row of
 *     voxels on every side that has air next to it. On grass, the green comes
 *     down with it, onto the voxel that is now on top.
 *   - Trims, off unless TERRAIN_TRIMS is 1. An air block with a solid block
 *     under it gets a row of voxels on
 *     its bottom layer along every side that has a solid block next to it,
 *     filling the inside corner where the floor meets the wall. It is made of
 *     the floor: green on grass. It stops short where the floor's own edge
 *     is lowered under it, rather than hanging in the air.
 *
 * Any number of either on one block, each side on its own. Outside the
 * terrain counts as air, like everywhere else. None of it reaches the
 * simulation: a vehicle still drives on the block tops (Terrain::ground_level).
 */

/**
 * Whether block_detail() gives air blocks trims. Off: the raised rows looked
 * worse than the plain step. Everything else about them is still here and
 * still tested, so 1 brings them back, here or with -DTERRAIN_TRIMS=1.
 */
#ifndef TERRAIN_TRIMS
#define TERRAIN_TRIMS 0
#endif

/**
 * How many voxels a block is drawn as along each side. A voxel is drawn one
 * world unit across and a simulation unit is one world unit (SimConvert), so
 * this has to be the simulation's BLOCK_SIZE for the terrain and the vehicles
 * on it to line up.
 */
constexpr int BLOCK_VOXELS = sim::BLOCK_SIZE;

// What each block type is drawn with, as ids in the map's palette (set up in
// VoxelMap's constructor)
constexpr VoxelID STONE_VOXEL = 13;   // GRAY
constexpr VoxelID DIRT_VOXEL = 7;     // BROWN
constexpr VoxelID GRASS_VOXEL = 2;    // DARKGREEN
constexpr VoxelID SAND_VOXEL = 14;
constexpr VoxelID SANDSTONE_VOXEL = 15;
constexpr VoxelID SNOW_VOXEL = 16;
constexpr VoxelID GRAVEL_VOXEL = 17;

// A block is drawn into a single chunk, which build_terrain_voxels() leans on
static_assert(CHUNK_SIZE % BLOCK_VOXELS == 0, "a block would straddle two chunks");

// The four sides of a block, as bits of BlockDetail's masks
constexpr uint8_t SIDE_X_POS = 1;
constexpr uint8_t SIDE_X_NEG = 2;
constexpr uint8_t SIDE_Y_POS = 4;
constexpr uint8_t SIDE_Y_NEG = 8;

/** The detail a block is drawn with, from its neighbours. See the top of this file. */
struct BlockDetail {
    // Sides whose top edge is lowered, on a solid block
    uint8_t lowered = 0;
    // Sides with a trim along the bottom, on an air block
    uint8_t trims = 0;
    // What the trims are made of: the block under them
    sim::BlockType trim_type = sim::BlockType::Air;
    // The lowered edges of that block under the trims. A trim leaves out its
    // voxels over them, so that where a lowered edge meets the end of a trim
    // the trim does not hang over the notch.
    uint8_t floor_lowered = 0;

    bool operator==(const BlockDetail&) const = default;
};

/** What block (x, y, z) of `terrain` is drawn with. */
BlockDetail block_detail(const sim::Terrain& terrain, int32_t x, int32_t y, int32_t z);

/**
 * The voxel at `in_block` (each axis 0 to BLOCK_VOXELS - 1, z up) of a block
 * of `type` drawn with `detail`. Grass is dirt with the top voxel of each
 * column green; every other block is its own colour all through. Air is 0
 * but for its trims. An empty detail is the plain cube.
 */
VoxelID block_voxel(sim::BlockType type, const BlockDetail& detail, Int3 in_block);

/** How many voxels a map has to be on each axis (z up) to hold `terrain`. */
Int3 terrain_voxel_size(const sim::Terrain& terrain);

/**
 * Empties the map and draws `terrain`'s land cells into it, block (x, y, z) at
 * voxels BLOCK_VOXELS * (x, y, z) onwards. Ocean cells are left out: they are
 * a flat sea floor and nothing else, which the WaterView draws as a plane
 * rather than as chunks of voxels, and leaving them out is what keeps a world
 * map's chunks down to its islands. Every chunk written is new, so it is
 * marked for remeshing and for its shadow volume.
 *
 * A map smaller than the terrain gets what fits, which is reported by
 * returning false.
 */
bool build_terrain_voxels(VoxelMap& map, const sim::Terrain& terrain);

/**
 * Draws one cell of `terrain` into the map as build_terrain_voxels() does, on
 * top of what is there: for a cell that has just become land (an island
 * placed in a running game) the map holds nothing there yet. The chunks round
 * the cell's edges are marked for remeshing, as their faces towards it were
 * drawn against ocean. An ocean cell draws nothing. False when part of the
 * cell does not fit the map.
 */
bool draw_terrain_cell(VoxelMap& map, const sim::Terrain& terrain, int32_t cell_x, int32_t cell_y);

#endif //BUSINESS_GAME_TERRAINVOXELS_HPP
