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
};

/** One past the highest BlockType, for refusing a number that is not one. */
constexpr uint8_t BLOCK_TYPE_COUNT = 4;

const char* block_type_name(BlockType type);

/**
 * How many simulation units a block is across, on every axis. A block is the
 * terrain's cell: the gameplay grid is made of them, and anything that sits on
 * the terrain sits on a block. Positions within a block are still in units
 * (and fractions of one, being Fixed), so a vehicle drives smoothly across it.
 */
constexpr int32_t BLOCK_SIZE = 4;

/** What generate_terrain() makes. The defaults are the game's map. */
struct TerrainSettings {
    // In blocks
    int32_t size_x = 32;
    int32_t size_y = 32;
    // 128 units, so 128 voxels on screen
    int32_t size_z = 32;
    // The seed of the noise's permutation, not of the simulation's Rng: the
    // terrain is the same whatever the game's seed is, until a game wants
    // otherwise. 123456 is what the presentation's terrain always used.
    uint32_t seed = 123456;
    // How far through the noise one block moves. The old voxel terrain moved
    // a twentieth per voxel, and a block is four of those.
    Fixed noise_scale = Fixed::from_ratio(1, 5);
    // How many blocks a noise sample of 1 stands, which sets how tall the
    // hills are and with them how steep: a slope is roughly hill_height *
    // noise_scale blocks per block at its steepest. Kept apart from size_z so
    // the world can have headroom without every slope turning into a cliff.
    // Anything past the top of the world is cut off at size_z.
    // 8 tops out at 6 blocks with nearly every slope a single block step; 32
    // reached 23 blocks, but four in ten neighbouring columns were more than a
    // block apart. Taller and still gentle wants a smaller noise_scale too.
    int32_t hill_height = 8;
    // How many blocks of dirt sit under the grass before the stone starts
    int32_t dirt_depth = 1;
};

/**
 * The ground, as a 3D grid of blocks: x and y across the map, z up, block
 * (x, y, z) covering units [x, x + 1) * BLOCK_SIZE on each axis.
 *
 * Blocks are the simulation's terrain. The presentation draws each one as
 * more detail than the simulation knows about, but nothing it draws flows
 * back here.
 */
class Terrain {
public:
    /** No blocks at all. */
    Terrain() = default;

    /** All air. Every size has to be positive, or the terrain is left empty. */
    Terrain(int32_t size_x, int32_t size_y, int32_t size_z);

    int32_t size_x() const { return sx; }
    int32_t size_y() const { return sy; }
    int32_t size_z() const { return sz; }

    bool in_bounds(int32_t x, int32_t y, int32_t z) const;

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
     * i32 size_x, size_y, size_z, then one byte per block, x fastest, then
     * y, then z. Raw: the default map is 4 KiB, small enough that compressing
     * it would not be worth what it costs the checksum on every call.
     */
    void write(ByteWriter& out) const;

    /**
     * What write() wrote. Refuses a negative size, a terrain too big to be a
     * real one, and a byte that is not a BlockType.
     */
    bool read(ByteReader& in);

    bool operator==(const Terrain&) const = default;

private:
    int32_t sx = 0;
    int32_t sy = 0;
    int32_t sz = 0;
    std::vector<BlockType> blocks;

    size_t index(int32_t x, int32_t y, int32_t z) const {
        return static_cast<size_t>(x)
             + static_cast<size_t>(y) * static_cast<size_t>(sx)
             + static_cast<size_t>(z) * static_cast<size_t>(sx) * static_cast<size_t>(sy);
    }
};

/** The most blocks a terrain read back from bytes may have, 64 MiB of them. */
constexpr int64_t MAX_TERRAIN_BLOCKS = int64_t{1} << 26;

/**
 * Terrain from Perlin noise: one sample per column, at the middle of the
 * column, picks how tall it stands. The top block is grass, the dirt_depth
 * blocks under it dirt, and everything below that stone. Every column has at
 * least its bottom block, so there is no hole through the world.
 *
 * The same settings give the same terrain on every machine.
 */
Terrain generate_terrain(const TerrainSettings& settings);

} // namespace sim

#endif //BUSINESS_GAME_SIM_TERRAIN_HPP
