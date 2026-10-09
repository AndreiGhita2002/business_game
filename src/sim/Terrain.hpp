//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_SIM_TERRAIN_HPP
#define BUSINESS_GAME_SIM_TERRAIN_HPP
#include <cstdint>
#include <vector>

#include "sim/Fixed.hpp"
#include "sim/Serial.hpp"

namespace sim {

/**
 * What a block of terrain is made of. The numbers are what a save and the
 * checksum hold, so a type keeps its number and a new one takes the next.
 */
enum class BlockType : uint8_t {
    Air = 0,
    Stone = 1,
    Dirt = 2,
    Grass = 3,
    // The rest came with the islands (sim/Island.hpp), for their biomes:
    // beaches and deserts, the rock under a desert, snowy ground, and the
    // shingle beaches of a snowy island
    Sand = 4,
    Sandstone = 5,
    Snow = 6,
    Gravel = 7,
};

/** One past the highest BlockType, for refusing a number that is not one. */
constexpr uint8_t BLOCK_TYPE_COUNT = 8;

const char* block_type_name(BlockType type);

/**
 * How many simulation units a block is across, on every axis. A block is the
 * terrain's unit of gameplay: the gameplay grid is made of them, and anything
 * that sits on the terrain sits on a block. Positions within a block are still
 * in units (and fractions of one, being Fixed), so a vehicle drives smoothly
 * across it.
 */
constexpr int32_t BLOCK_SIZE = 4;

/**
 * How many blocks a cell is across, and how tall the world is. The world is a
 * grid of cells (docs/terrain.md): each one is either ocean, a flat sea floor
 * and nothing else, or part of an island.
 */
constexpr int32_t CELL_BLOCKS = 64;

/** The world a new game gets, in cells on each side, and the most it can have. */
constexpr int32_t DEFAULT_WORLD_CELLS = 10;
// 32 cells is 2048 blocks, 8192 voxels, a side. Ocean costs next to nothing
// on either side of the split, so the limit is set by what the presentation
// has to walk every frame (the water's chunks) rather than by memory.
constexpr int32_t MAX_WORLD_CELLS = 32;

/** How many blocks of stone an ocean cell's floor is. */
constexpr int32_t SEA_FLOOR_BLOCKS = 2;

/**
 * The water level a new game starts with, and the lowest one there can be.
 * A level is a unit layer: the water fills layers 0 to the level, one layer
 * per unit of z, so its top is at level + 1. 0 is the lowest because layer 0
 * is the bottom of the world. Here rather than with the rest of the water in
 * Simulation.hpp because the terrain is generated for a water level.
 */
// 15 puts the water's top at 16 units, four blocks up: two blocks of water
// over the sea floor.
constexpr int32_t DEFAULT_WATER_LEVEL = 16;
constexpr int32_t MIN_WATER_LEVEL = 0;

/** What generate_terrain() makes. The defaults are the game's world. */
struct TerrainSettings {
    // How many cells the world is on each side. Brought down to
    // MAX_WORLD_CELLS; 0 or less makes no terrain at all, which a load uses
    // to skip generating one it is about to replace.
    int32_t cells_x = DEFAULT_WORLD_CELLS;
    int32_t cells_y = DEFAULT_WORLD_CELLS;
    // Where the island comes from: its shape, relief and biome are drawn from
    // this (random_island_spec() in sim/Island.hpp), and so is the seed of its
    // noise. Not the simulation's Rng: the terrain is the same whatever the
    // game's seed is.
    uint32_t seed = 123456;
    // The water level the terrain is made for, in units (see
    // DEFAULT_WATER_LEVEL), and the one a game made from these settings
    // starts with. Every block wholly under the water is stone, whatever it
    // would have been: a sea floor rather than drowned grass.
    int32_t water_level = DEFAULT_WATER_LEVEL;
    // One island in the middle of the world, or nothing but ocean
    bool centre_island = true;
};

/**
 * The ground, as a 3D grid of blocks: x and y across the map, z up, block
 * (x, y, z) covering units [x, x + 1) * BLOCK_SIZE on each axis.
 *
 * The blocks are held a cell at a time. A cell nothing has written to is
 * ocean: `sea_floor` blocks of stone in every column and air above, which
 * takes no memory, so a world that is mostly sea is cheap whatever its size.
 * Writing a block that differs from that gives the cell blocks of its own,
 * with the sea floor copied in first. Every query answers the same for an
 * ocean cell as for a cell holding the same blocks.
 *
 * Blocks are the simulation's terrain. The presentation draws each one as
 * more detail than the simulation knows about, but nothing it draws flows
 * back here.
 */
class Terrain {
public:
    /** No blocks at all. */
    Terrain() = default;

    /**
     * cells_x by cells_y cells of ocean, each cell_blocks across and size_z
     * tall, with sea_floor blocks of stone under every column. A size that is
     * not positive, or a sea floor outside [0, size_z], leaves it empty.
     */
    Terrain(int32_t cells_x, int32_t cells_y, int32_t cell_blocks = CELL_BLOCKS,
            int32_t size_z = CELL_BLOCKS, int32_t sea_floor = SEA_FLOOR_BLOCKS);

    /**
     * Nothing but air, size_x by size_y by size_z blocks: every column its own
     * cell, and no sea floor. For a small terrain built by hand, block by
     * block, which is what the tests do.
     */
    static Terrain of_blocks(int32_t size_x, int32_t size_y, int32_t size_z);

    // In blocks
    int32_t size_x() const { return cx * cb; }
    int32_t size_y() const { return cy * cb; }
    int32_t size_z() const { return sz; }

    int32_t cells_x() const { return cx; }
    int32_t cells_y() const { return cy; }
    int32_t cell_blocks() const { return cb; }
    /** How many blocks of stone an ocean cell's columns are. */
    int32_t sea_floor() const { return floor_blocks; }

    bool in_bounds(int32_t x, int32_t y, int32_t z) const;

    /** Whether cell (x, y) is ocean, holding no blocks of its own. False outside the terrain. */
    bool is_ocean_cell(int32_t cell_x, int32_t cell_y) const;

    /** Turns cell (x, y) back into ocean. Nothing happens outside the terrain. */
    void reset_cell(int32_t cell_x, int32_t cell_y);

    /** The block at (x, y, z). Outside the terrain is air. */
    BlockType get(int32_t x, int32_t y, int32_t z) const;

    /** Sets a block, or does nothing and says so when it is outside the terrain. */
    bool set(int32_t x, int32_t y, int32_t z, BlockType type);

    bool is_solid(int32_t x, int32_t y, int32_t z) const { return get(x, y, z) != BlockType::Air; }

    /**
     * How many blocks tall column (x, y) stands: one above its highest solid
     * block, so 0 for a column of air or one outside the terrain.
     */
    int32_t column_height(int32_t x, int32_t y) const;

    /**
     * The top of the ground under a point given in units, in units: the
     * height of the column the point is over, times BLOCK_SIZE.
     */
    Fixed ground_level(Fixed x, Fixed y) const;

    /**
     * i32 cells_x, cells_y, cell_blocks, size_z, sea_floor, then a cell at a
     * time, x fastest: u8 0 for ocean, or u8 1 and then its blocks, a byte
     * each, x fastest, then y, then z. Raw: an island is a few cells, and the
     * ocean round it a byte a cell.
     */
    void write(ByteWriter& out) const;

    /**
     * What write() wrote. Refuses a negative size, a terrain too big to be a
     * real one, a cell that is neither ocean nor land, and a byte that is not
     * a BlockType.
     */
    bool read(ByteReader& in);

    bool operator==(const Terrain&) const = default;

private:
    int32_t cx = 0;
    int32_t cy = 0;
    int32_t cb = 0;
    int32_t sz = 0;
    int32_t floor_blocks = 0;
    // A cell each, x fastest. Empty for an ocean cell, otherwise cb * cb * sz
    // blocks, x fastest, then y, then z.
    std::vector<std::vector<BlockType>> cells;

    size_t cell_index(const int32_t cell_x, const int32_t cell_y) const {
        return static_cast<size_t>(cell_x) + static_cast<size_t>(cell_y) * static_cast<size_t>(cx);
    }
    // Where block (x, y, z) of the whole terrain sits among its cell's blocks
    size_t block_index(int32_t x, int32_t y, int32_t z) const;
    // What an ocean cell holds at height z
    BlockType ocean_block(const int32_t z) const {
        return z < floor_blocks ? BlockType::Stone : BlockType::Air;
    }
};

/** The most blocks a terrain read back from bytes may hold in its land cells, 64 MiB of them. */
constexpr int64_t MAX_TERRAIN_BLOCKS = int64_t{1} << 26;
/**
 * The most cells along a side, and blocks across a cell or up, that a terrain
 * read back from bytes may have. Looser than MAX_WORLD_CELLS, which is the
 * game's own limit, so a terrain built of one column cells still reads.
 */
constexpr int32_t MAX_TERRAIN_CELLS_PER_SIDE = 256;
constexpr int32_t MAX_TERRAIN_CELL_BLOCKS = 256;

/**
 * The world for `settings`: cells_x by cells_y cells of ocean and, unless it
 * is asked not to, one island in the middle drawn from the seed
 * (random_island_spec() and place_island() in sim/Island.hpp). An island
 * that does not fit the world in any of its turns is left out.
 *
 * The same settings give the same terrain on every machine.
 */
Terrain generate_terrain(const TerrainSettings& settings);

/**
 * Whether the block at layer z is wholly under water at `water_level`: its
 * top, (z + 1) * BLOCK_SIZE units up, is no higher than the water's top,
 * water_level + 1. A block only part way in, a shore, is not.
 */
constexpr bool block_under_water(const int32_t z, const int32_t water_level) {
    return int64_t{z + 1} * BLOCK_SIZE <= int64_t{water_level} + 1;
}

} // namespace sim

#endif //BUSINESS_GAME_SIM_TERRAIN_HPP
