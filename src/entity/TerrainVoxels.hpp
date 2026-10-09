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
 * This is where a block gets more detail than the simulation gives it. Today
 * a block's voxels depend only on its type and where they sit inside it.
 */

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

/**
 * The voxel at `in_block` (each axis 0 to BLOCK_VOXELS - 1, z up) of a block
 * of `type`. Stone is grey and dirt brown all through; grass is dirt with its
 * top layer of voxels green. Air is 0.
 */
VoxelID block_voxel(sim::BlockType type, Int3 in_block);

/** How many voxels across a map has to be to hold `terrain`. */
Int2 terrain_voxel_size(const sim::Terrain& terrain);

/**
 * Empties the map and draws `terrain` into it, block (x, y, z) at voxels
 * BLOCK_VOXELS * (x, y, z) onwards. Every chunk is marked for remeshing and
 * for its shadow volume.
 *
 * A map smaller than the terrain gets what fits, which is reported by
 * returning false: the map is one chunk tall, so that includes a terrain more
 * than CHUNK_SIZE / BLOCK_VOXELS blocks high.
 */
bool build_terrain_voxels(VoxelMap& map, const sim::Terrain& terrain);

#endif //BUSINESS_GAME_TERRAINVOXELS_HPP
