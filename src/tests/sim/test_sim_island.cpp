// The islands: their footprints, the specs drawn from a seed, where one is put,
// and what the generator promises about the island it makes - the coast
// meeting the ocean at the sea floor, slopes that can be climbed, taller
// relief for rougher elevations, and each biome's blocks on top.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <set>

#include "sim/Island.hpp"
#include "sim/Terrain.hpp"

using namespace sim;

namespace {

/** A world of `cells` by `cells` ocean with `spec`'s island at cell (1, 1). */
Terrain world_with(const IslandSpec& spec, const int32_t cells = 4) {
    Terrain terrain(cells, cells);
    REQUIRE(place_island(terrain, 1, 1, spec, DEFAULT_WATER_LEVEL));
    return terrain;
}

/** The biggest step between two neighbouring columns anywhere in the terrain. */
int32_t steepest_step(const Terrain& t) {
    int32_t step = 0;
    for (int32_t y = 0; y < t.size_y(); ++y) {
        for (int32_t x = 0; x < t.size_x(); ++x) {
            const int32_t c = t.column_height(x, y);
            if (x + 1 < t.size_x()) step = std::max(step, std::abs(c - t.column_height(x + 1, y)));
            if (y + 1 < t.size_y()) step = std::max(step, std::abs(c - t.column_height(x, y + 1)));
        }
    }
    return step;
}

int32_t tallest_column(const Terrain& t) {
    int32_t top = 0;
    for (int32_t y = 0; y < t.size_y(); ++y) {
        for (int32_t x = 0; x < t.size_x(); ++x) top = std::max(top, t.column_height(x, y));
    }
    return top;
}

/** Every top block of a column standing out of the water. */
std::set<BlockType> land_tops(const Terrain& t) {
    std::set<BlockType> tops;
    for (int32_t y = 0; y < t.size_y(); ++y) {
        for (int32_t x = 0; x < t.size_x(); ++x) {
            const int32_t height = t.column_height(x, y);
            if (!block_under_water(height - 1, DEFAULT_WATER_LEVEL)) tops.insert(t.get(x, y, height - 1));
        }
    }
    return tops;
}

IslandSpec spec_of(const Elevation elevation, const Biome biome) {
    IslandSpec spec;
    spec.shape = IslandShape::Square;
    spec.elevation = elevation;
    spec.biome = biome;
    spec.seed = 777;
    return spec;
}

} // namespace

TEST_CASE("Island: every footprint is four cells in a box from the origin", "[sim][island]") {
    for (uint8_t s = 0; s < ISLAND_SHAPE_COUNT; ++s) {
        for (uint8_t r = 0; r < 4; ++r) {
            const Footprint f = island_footprint(static_cast<IslandShape>(s), r);
            REQUIRE(f.cells.size() == 4);
            const std::set<std::pair<int32_t, int32_t>> distinct = [&f] {
                std::set<std::pair<int32_t, int32_t>> d;
                for (const CellPos& c : f.cells) d.insert({c.x, c.y});
                return d;
            }();
            REQUIRE(distinct.size() == 4);

            int32_t min_x = 99, min_y = 99;
            for (const CellPos& c : f.cells) {
                REQUIRE(c.x < f.width);
                REQUIRE(c.y < f.height);
                REQUIRE(f.contains(c.x, c.y));
                min_x = std::min(min_x, c.x);
                min_y = std::min(min_y, c.y);
            }
            REQUIRE(min_x == 0);
            REQUIRE(min_y == 0);

            // Four quarter turns are no turn at all
            REQUIRE(island_footprint(static_cast<IslandShape>(s), r + 4).cells == f.cells);
        }
    }

    // A line lies along x, and along y a quarter turn later
    REQUIRE(island_footprint(IslandShape::Line, 0).width == 4);
    REQUIRE(island_footprint(IslandShape::Line, 0).height == 1);
    REQUIRE(island_footprint(IslandShape::Line, 1).width == 1);
    REQUIRE(island_footprint(IslandShape::Line, 1).height == 4);
    // A square is a square whichever way round
    REQUIRE(island_footprint(IslandShape::Square, 1).cells == island_footprint(IslandShape::Square, 0).cells);
    // A T is three across with one under the middle, which a half turn puts on top
    REQUIRE(island_footprint(IslandShape::T, 0).contains(1, 1));
    REQUIRE_FALSE(island_footprint(IslandShape::T, 0).contains(0, 1));
    REQUIRE(island_footprint(IslandShape::T, 2).contains(1, 0));
    REQUIRE_FALSE(island_footprint(IslandShape::T, 2).contains(0, 0));
}

TEST_CASE("Island: a spec is drawn from its seed, and every choice comes up", "[sim][island]") {
    REQUIRE(random_island_spec(5) == random_island_spec(5));

    std::set<uint8_t> shapes, rotations, elevations, biomes;
    std::set<uint32_t> seeds;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        const IslandSpec spec = random_island_spec(seed);
        shapes.insert(static_cast<uint8_t>(spec.shape));
        rotations.insert(spec.rotation);
        elevations.insert(static_cast<uint8_t>(spec.elevation));
        biomes.insert(static_cast<uint8_t>(spec.biome));
        seeds.insert(spec.seed);
    }
    REQUIRE(shapes.size() == ISLAND_SHAPE_COUNT);
    REQUIRE(rotations.size() == 4);
    REQUIRE(elevations.size() == ELEVATION_COUNT);
    REQUIRE(biomes.size() == BIOME_COUNT);
    REQUIRE(seeds.size() > 190);
}

TEST_CASE("Island: centred in the world, when it fits", "[sim][island]") {
    const Terrain world(10, 10);
    IslandSpec spec;
    spec.shape = IslandShape::Line;
    CellPos at;
    REQUIRE(centred_island_cell(world, spec, &at));
    REQUIRE(at == CellPos{3, 4});

    spec.shape = IslandShape::L;   // two across, three down
    REQUIRE(centred_island_cell(world, spec, &at));
    REQUIRE(at == CellPos{4, 3});

    // A line does not fit three cells across, but fits once it is turned
    const Terrain narrow(3, 10);
    spec.shape = IslandShape::Line;
    REQUIRE_FALSE(centred_island_cell(narrow, spec, &at));
    spec.rotation = 1;
    REQUIRE(centred_island_cell(narrow, spec, &at));
    REQUIRE(at == CellPos{1, 3});
}

TEST_CASE("Island: refused off the world, over land, and on small cells", "[sim][island]") {
    IslandSpec spec;   // a square, two by two
    Terrain terrain(4, 4);

    REQUIRE_FALSE(place_island(terrain, 3, 0, spec, DEFAULT_WATER_LEVEL));
    REQUIRE_FALSE(place_island(terrain, -1, 0, spec, DEFAULT_WATER_LEVEL));
    REQUIRE(terrain == Terrain(4, 4));

    REQUIRE(place_island(terrain, 0, 0, spec, DEFAULT_WATER_LEVEL));
    const Terrain once = terrain;
    // Overlapping the first by one cell
    REQUIRE_FALSE(place_island(terrain, 1, 1, spec, DEFAULT_WATER_LEVEL));
    REQUIRE(terrain == once);
    // Beside it is fine
    REQUIRE(place_island(terrain, 2, 2, spec, DEFAULT_WATER_LEVEL));

    // The coast is laid out in blocks, which a small cell has too few of
    Terrain small(4, 4, 16);
    REQUIRE_FALSE(place_island(small, 0, 0, spec, DEFAULT_WATER_LEVEL));
}

TEST_CASE("Island: the same island wherever it is put", "[sim][island]") {
    IslandSpec spec;
    spec.shape = IslandShape::Zigzag;
    spec.seed = 99;

    Terrain a(6, 6);
    Terrain b(6, 6);
    REQUIRE(place_island(a, 0, 0, spec, DEFAULT_WATER_LEVEL));
    REQUIRE(place_island(b, 2, 3, spec, DEFAULT_WATER_LEVEL));

    const int32_t dx = 2 * CELL_BLOCKS;
    const int32_t dy = 3 * CELL_BLOCKS;
    for (int32_t y = 0; y < 2 * CELL_BLOCKS; y += 3) {
        for (int32_t x = 0; x < 3 * CELL_BLOCKS; x += 3) {
            REQUIRE(a.column_height(x, y) == b.column_height(x + dx, y + dy));
            const int32_t top = a.column_height(x, y) - 1;
            REQUIRE(a.get(x, y, top) == b.get(x + dx, y + dy, top));
        }
    }
}

TEST_CASE("Island: the coast meets the ocean at the sea floor", "[sim][island]") {
    for (uint8_t s = 0; s < ISLAND_SHAPE_COUNT; ++s) {
        IslandSpec spec;
        spec.shape = static_cast<IslandShape>(s);
        spec.elevation = Elevation::Mountainous;
        spec.seed = 31 + s;
        Terrain terrain(6, 6);
        REQUIRE(place_island(terrain, 1, 1, spec, DEFAULT_WATER_LEVEL));

        // Every column of a land cell that is on the edge of a cell next to
        // ocean (or the world's edge) is the sea floor, so an island never
        // ends in a cliff where its cells do
        for (int32_t y = 0; y < terrain.size_y(); ++y) {
            for (int32_t x = 0; x < terrain.size_x(); ++x) {
                const int32_t cx = x / CELL_BLOCKS, cy = y / CELL_BLOCKS;
                if (terrain.is_ocean_cell(cx, cy)) continue;
                const int32_t lx = x % CELL_BLOCKS, ly = y % CELL_BLOCKS;
                const bool ocean_beside =
                    (lx == 0 && terrain.is_ocean_cell(cx - 1, cy)) ||
                    (lx == CELL_BLOCKS - 1 && terrain.is_ocean_cell(cx + 1, cy)) ||
                    (ly == 0 && terrain.is_ocean_cell(cx, cy - 1)) ||
                    (ly == CELL_BLOCKS - 1 && terrain.is_ocean_cell(cx, cy + 1));
                if (ocean_beside) REQUIRE(terrain.column_height(x, y) == SEA_FLOOR_BLOCKS);
            }
        }

        // And there is an island at all
        REQUIRE(tallest_column(terrain) > SEA_FLOOR_BLOCKS + 3);
    }
}

TEST_CASE("Island: rougher elevations stand taller, and every slope can be climbed", "[sim][island]") {
    const Terrain flat = world_with(spec_of(Elevation::Flat, Biome::Grassland));
    const Terrain hilly = world_with(spec_of(Elevation::Hilly, Biome::Grassland));
    const Terrain mountainous = world_with(spec_of(Elevation::Mountainous, Biome::Grassland));

    REQUIRE(tallest_column(flat) < tallest_column(hilly));
    REQUIRE(tallest_column(hilly) < tallest_column(mountainous));
    // Never through the top of the world
    REQUIRE(tallest_column(mountainous) <= CELL_BLOCKS);

    // A block a step, or two in the mountains
    REQUIRE(steepest_step(flat) <= 1);
    REQUIRE(steepest_step(hilly) <= 1);
    REQUIRE(steepest_step(mountainous) <= 2);
}

TEST_CASE("Island: each biome has its own ground", "[sim][island]") {
    const std::set<BlockType> grassland = land_tops(world_with(spec_of(Elevation::Hilly, Biome::Grassland)));
    REQUIRE(grassland.count(BlockType::Grass) == 1);
    REQUIRE(grassland.count(BlockType::Sand) == 1);   // the beach

    const std::set<BlockType> desert = land_tops(world_with(spec_of(Elevation::Hilly, Biome::Desert)));
    REQUIRE(desert.count(BlockType::Sand) == 1);
    REQUIRE(desert.count(BlockType::Grass) == 0);
    REQUIRE(desert.count(BlockType::Snow) == 0);

    const std::set<BlockType> snowy = land_tops(world_with(spec_of(Elevation::Hilly, Biome::Snowy)));
    REQUIRE(snowy.count(BlockType::Snow) == 1);
    REQUIRE(snowy.count(BlockType::Gravel) == 1);   // the beach
    REQUIRE(snowy.count(BlockType::Grass) == 0);

    // Mountains on a grassland go to bare rock, and then snow, on top
    const std::set<BlockType> peaks = land_tops(world_with(spec_of(Elevation::Mountainous, Biome::Grassland)));
    REQUIRE(peaks.count(BlockType::Stone) == 1);
}
