//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_SIM_ISLAND_HPP
#define BUSINESS_GAME_SIM_ISLAND_HPP
#include <cstdint>
#include <vector>

#include "sim/Terrain.hpp"

namespace sim {

/**
 * The islands of docs/terrain.md: a few cells of the world turned from ocean
 * into land.
 *
 * An island is generated from an IslandSpec alone, in its own coordinates, so
 * the same spec makes the same island wherever it is put. The steps, all in
 * integers and Fixed:
 *
 *   1. The footprint: the cells it covers, a tetromino in one of four turns.
 *   2. How far every column is from the edge of the footprint, in blocks
 *      (a two pass chamfer distance, 5 for a straight step and 7 for a
 *      diagonal one).
 *   3. The coast: that distance pushed in by ISLAND_COAST_MARGIN and wobbled
 *      by low frequency noise, so the island is not the shape of its cells.
 *      Below 0 is sea, deepening away from the beach.
 *   4. The relief, by elevation type: summed octaves of Perlin noise, ridged
 *      for mountains, ramped in from the beach so the coast stays low.
 *   5. A slope limit: no column more than the elevation's max step above any
 *      of its four neighbours, so every slope can be climbed. The columns
 *      near the footprint's edge are pinned to the sea floor first, so an
 *      island always meets the ocean cells round it.
 *   6. The columns filled in by biome: the top block, what is under it, and
 *      stone below; stone under the water; sand or gravel on the beach.
 */

/** The footprint's shape, the tetrominoes of docs/terrain.md. */
enum class IslandShape : uint8_t {
    Square = 0,   // O
    Line = 1,     // I
    L = 2,
    T = 3,
    Zigzag = 4,   // S
};
constexpr uint8_t ISLAND_SHAPE_COUNT = 5;

/** How rough the island is, which picks its relief (see the top of this file). */
enum class Elevation : uint8_t {
    Flat = 0,
    Hilly = 1,
    Mountainous = 2,
};
constexpr uint8_t ELEVATION_COUNT = 3;

/** What the island is made of on top. */
enum class Biome : uint8_t {
    Grassland = 0,
    Desert = 1,
    Snowy = 2,
};
constexpr uint8_t BIOME_COUNT = 3;

const char* island_shape_name(IslandShape shape);
const char* elevation_name(Elevation elevation);
const char* biome_name(Biome biome);

struct CellPos {
    int32_t x = 0;
    int32_t y = 0;

    bool operator==(const CellPos&) const = default;
};

/** The cells an island covers, from (0, 0), and the box round them. */
struct Footprint {
    // In order along y and then x
    std::vector<CellPos> cells;
    int32_t width = 0;
    int32_t height = 0;

    bool contains(int32_t x, int32_t y) const;
};

/** The cells of `shape` turned `rotation` quarter turns, moved so the box round them starts at (0, 0). */
Footprint island_footprint(IslandShape shape, uint8_t rotation);

/** Everything an island is generated from. */
struct IslandSpec {
    IslandShape shape = IslandShape::Square;
    // Quarter turns, 0 to 3
    uint8_t rotation = 0;
    Elevation elevation = Elevation::Hilly;
    Biome biome = Biome::Grassland;
    // The noise's seed
    uint32_t seed = 0;

    bool operator==(const IslandSpec&) const = default;
};

/** A spec drawn at random, every choice equally likely, from an Rng seeded with `seed`. */
IslandSpec random_island_spec(uint32_t seed);

/**
 * The cell to put `spec`'s footprint at so it sits in the middle of the
 * terrain, rounded towards the origin. False when it does not fit.
 */
bool centred_island_cell(const Terrain& terrain, const IslandSpec& spec, CellPos* out);

// How the coast is laid out, in blocks: how far in from the footprint's edge
// it is on average, how far the noise moves it either way, and how far the
// columns nearest the edge are pinned to the sea floor whatever the coast
// does. The margin less the wobble has to stay above the pinned edge, or the
// coast can reach the next cell.
constexpr int32_t ISLAND_COAST_MARGIN = 14;
constexpr int32_t ISLAND_COAST_WOBBLE = 8;
constexpr int32_t ISLAND_EDGE_FLOOR = 3;
static_assert(ISLAND_COAST_MARGIN - ISLAND_COAST_WOBBLE > ISLAND_EDGE_FLOOR,
              "the coast could reach the edge of the footprint");

/**
 * Generates the island `spec` with its footprint's corner at cell
 * (cell_x, cell_y), for water at `water_level`. Every cell the footprint
 * covers is replaced whole.
 *
 * Refuses, changing nothing, a footprint that hangs off the terrain or covers
 * a cell that is already land, and a terrain whose cells are smaller than
 * CELL_BLOCKS - the coast is laid out in blocks and would not fit.
 */
bool place_island(Terrain& terrain, int32_t cell_x, int32_t cell_y, const IslandSpec& spec,
                  int32_t water_level);

} // namespace sim

#endif //BUSINESS_GAME_SIM_ISLAND_HPP
