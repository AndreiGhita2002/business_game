// The simulation's terrain: the fixed point noise it is generated from, the
// block grid and its queries, what generation makes, and the terrain going
// into the checksum and through a save.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <set>

#include "sim/Noise.hpp"
#include "sim/Save.hpp"
#include "sim/Simulation.hpp"
#include "sim/Terrain.hpp"

using namespace sim;

namespace {

/** Overwrites the u32 at `offset`. */
void poke_u32(std::vector<uint8_t>& bytes, const size_t offset, const uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t peek_u32(const std::vector<uint8_t>& bytes, const size_t offset) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(bytes[offset + i]) << (8 * i);
    return v;
}

/** Where the section tagged `tag` starts in a save, or 0. */
size_t find_section(const std::vector<uint8_t>& bytes, const uint32_t tag) {
    size_t at = 12;
    while (at + 12 <= bytes.size()) {
        if (peek_u32(bytes, at) == tag) return at;
        at += 12 + peek_u32(bytes, at + 8);
    }
    return 0;
}

} // namespace

TEST_CASE("Noise: the permutation is one, and depends on the seed", "[sim][terrain][noise]") {
    const PerlinNoise a(123456);
    const PerlinNoise b(123456);
    const PerlinNoise c(654321);

    const std::set<uint8_t> values(a.permutation().begin(), a.permutation().end());
    REQUIRE(values.size() == 256);
    REQUIRE(a.permutation() == b.permutation());
    REQUIRE(a.permutation() != c.permutation());
}

TEST_CASE("Noise: zero on the lattice, and small in between", "[sim][terrain][noise]") {
    const PerlinNoise noise(123456);

    // Every gradient is dotted with a zero offset at its own corner
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            REQUIRE(noise.noise3d(Fixed::from_int(x), Fixed::from_int(y), Fixed::from_int(2)) == Fixed{});
        }
    }

    // Improved Perlin noise stays inside [-1, 1]; a little slack for rounding
    const Fixed bound = Fixed::from_int(1) + Fixed::from_ratio(1, 100);
    bool varied = false;
    const Fixed first = noise.noise2d(Fixed{}, Fixed{});
    for (int i = -40; i < 40; ++i) {
        for (int j = -40; j < 40; ++j) {
            const Fixed v = noise.noise2d(Fixed::from_ratio(i, 7), Fixed::from_ratio(j, 9));
            REQUIRE(v <= bound);
            REQUIRE(v >= -bound);
            if (v != first) varied = true;
        }
    }
    REQUIRE(varied);
}

TEST_CASE("Terrain: blocks in and out of bounds", "[sim][terrain]") {
    Terrain terrain = Terrain::of_blocks(4, 3, 2);
    REQUIRE(terrain.size_x() == 4);
    REQUIRE(terrain.size_y() == 3);
    REQUIRE(terrain.size_z() == 2);
    REQUIRE(terrain.get(0, 0, 0) == BlockType::Air);

    REQUIRE(terrain.set(3, 2, 1, BlockType::Stone));
    REQUIRE(terrain.get(3, 2, 1) == BlockType::Stone);
    REQUIRE(terrain.is_solid(3, 2, 1));

    // Outside is air, and stays air
    REQUIRE_FALSE(terrain.set(4, 0, 0, BlockType::Dirt));
    REQUIRE_FALSE(terrain.set(0, -1, 0, BlockType::Dirt));
    REQUIRE_FALSE(terrain.set(0, 0, 2, BlockType::Dirt));
    REQUIRE(terrain.get(4, 0, 0) == BlockType::Air);
    REQUIRE(terrain.get(-1, 0, 0) == BlockType::Air);

    // A size that is not positive leaves no terrain at all
    const Terrain empty = Terrain::of_blocks(0, 5, 5);
    REQUIRE(empty.size_x() == 0);
    REQUIRE(empty.size_y() == 0);
    REQUIRE(empty.size_z() == 0);
}

TEST_CASE("Terrain: column heights and the ground under a point", "[sim][terrain]") {
    Terrain terrain = Terrain::of_blocks(3, 2, 4);
    terrain.set(0, 0, 0, BlockType::Stone);
    terrain.set(0, 0, 1, BlockType::Grass);
    terrain.set(1, 0, 0, BlockType::Grass);
    // A floating block counts: the height is one above the highest solid one
    terrain.set(2, 1, 3, BlockType::Stone);

    REQUIRE(terrain.column_height(0, 0) == 2);
    REQUIRE(terrain.column_height(1, 0) == 1);
    REQUIRE(terrain.column_height(2, 0) == 0);
    REQUIRE(terrain.column_height(2, 1) == 4);
    REQUIRE(terrain.column_height(-1, 0) == 0);
    REQUIRE(terrain.column_height(3, 0) == 0);

    // In units: column 0 is x in [0, 4), column 1 is [4, 8)
    REQUIRE(terrain.ground_level(Fixed{}, Fixed{}) == Fixed::from_int(2 * BLOCK_SIZE));
    REQUIRE(terrain.ground_level(Fixed::from_ratio(399, 100), Fixed::from_int(3)) == Fixed::from_int(2 * BLOCK_SIZE));
    REQUIRE(terrain.ground_level(Fixed::from_int(4), Fixed{}) == Fixed::from_int(BLOCK_SIZE));
    REQUIRE(terrain.ground_level(Fixed::from_int(9), Fixed::from_int(5)) == Fixed::from_int(4 * BLOCK_SIZE));
    // Just west of the origin is off the terrain, not column 0
    REQUIRE(terrain.ground_level(Fixed::from_ratio(-1, 100), Fixed{}) == Fixed{});
    REQUIRE(terrain.ground_level(Fixed::from_int(12), Fixed{}) == Fixed{});
}

TEST_CASE("Terrain: an ocean cell is a sea floor until something is written to it", "[sim][terrain]") {
    // Three by two cells of eight blocks, six tall, with two of sea floor
    Terrain terrain(3, 2, 8, 6, 2);
    REQUIRE(terrain.size_x() == 24);
    REQUIRE(terrain.size_y() == 16);
    REQUIRE(terrain.size_z() == 6);
    REQUIRE(terrain.cells_x() == 3);
    REQUIRE(terrain.cells_y() == 2);
    REQUIRE(terrain.sea_floor() == 2);

    REQUIRE(terrain.is_ocean_cell(0, 0));
    REQUIRE(terrain.get(5, 5, 0) == BlockType::Stone);
    REQUIRE(terrain.get(5, 5, 1) == BlockType::Stone);
    REQUIRE(terrain.get(5, 5, 2) == BlockType::Air);
    REQUIRE(terrain.column_height(5, 5) == 2);
    REQUIRE(terrain.ground_level(Fixed::from_int(21), Fixed::from_int(21)) == Fixed::from_int(2 * BLOCK_SIZE));

    // Writing what is already there leaves it ocean
    REQUIRE(terrain.set(5, 5, 1, BlockType::Stone));
    REQUIRE(terrain.set(5, 5, 4, BlockType::Air));
    REQUIRE(terrain.is_ocean_cell(0, 0));

    // Anything else gives the cell blocks of its own, the sea floor kept
    REQUIRE(terrain.set(5, 5, 3, BlockType::Dirt));
    REQUIRE_FALSE(terrain.is_ocean_cell(0, 0));
    REQUIRE(terrain.is_ocean_cell(1, 0));
    REQUIRE(terrain.get(5, 5, 3) == BlockType::Dirt);
    REQUIRE(terrain.get(6, 6, 0) == BlockType::Stone);
    REQUIRE(terrain.get(6, 6, 2) == BlockType::Air);
    REQUIRE(terrain.column_height(5, 5) == 4);
    REQUIRE(terrain.column_height(6, 6) == 2);

    // And back again
    terrain.reset_cell(0, 0);
    REQUIRE(terrain.is_ocean_cell(0, 0));
    REQUIRE(terrain.get(5, 5, 3) == BlockType::Air);
    REQUIRE(terrain == Terrain(3, 2, 8, 6, 2));

    // Outside the terrain is no cell at all, ocean or not
    REQUIRE_FALSE(terrain.is_ocean_cell(3, 0));
    REQUIRE_FALSE(terrain.is_ocean_cell(0, -1));

    // A sea floor taller than the world is no terrain
    REQUIRE(Terrain(2, 2, 8, 6, 7).size_x() == 0);
}

TEST_CASE("Terrain: the default world is ocean round one island in the middle", "[sim][terrain]") {
    const TerrainSettings settings;
    const Terrain terrain = generate_terrain(settings);
    REQUIRE(terrain.cells_x() == DEFAULT_WORLD_CELLS);
    REQUIRE(terrain.cells_y() == DEFAULT_WORLD_CELLS);
    REQUIRE(terrain.size_x() == DEFAULT_WORLD_CELLS * CELL_BLOCKS);
    REQUIRE(terrain.size_z() == CELL_BLOCKS);
    REQUIRE(terrain.sea_floor() == SEA_FLOOR_BLOCKS);

    // Four cells of land, a tetromino, touching the middle of the world
    int land = 0;
    bool middle = false;
    for (int32_t cy = 0; cy < terrain.cells_y(); ++cy) {
        for (int32_t cx = 0; cx < terrain.cells_x(); ++cx) {
            if (terrain.is_ocean_cell(cx, cy)) continue;
            land++;
            if ((cx == 4 || cx == 5) && (cy == 4 || cy == 5)) middle = true;
        }
    }
    REQUIRE(land == 4);
    REQUIRE(middle);

    // Every column: at least the sea floor, stone under the water, air above
    // the top, and some of it standing out of the water
    bool above_water = false;
    for (int32_t y = 0; y < terrain.size_y(); ++y) {
        for (int32_t x = 0; x < terrain.size_x(); ++x) {
            const int32_t height = terrain.column_height(x, y);
            REQUIRE(height >= SEA_FLOOR_BLOCKS);
            if (terrain.is_ocean_cell(x / CELL_BLOCKS, y / CELL_BLOCKS)) {
                REQUIRE(height == SEA_FLOOR_BLOCKS);
                continue;
            }
            if (!block_under_water(height - 1, settings.water_level)) above_water = true;
            for (int32_t z = 0; z < terrain.size_z(); ++z) {
                const BlockType block = terrain.get(x, y, z);
                if (z >= height) REQUIRE(block == BlockType::Air);
                else if (block_under_water(z, settings.water_level)) REQUIRE(block == BlockType::Stone);
                else REQUIRE(block != BlockType::Air);
            }
        }
    }
    REQUIRE(above_water);
}

TEST_CASE("Terrain: the world is any size up to the limit", "[sim][terrain]") {
    TerrainSettings settings;
    settings.cells_x = 3;
    settings.cells_y = 7;
    const Terrain narrow = generate_terrain(settings);
    REQUIRE(narrow.size_x() == 3 * CELL_BLOCKS);
    REQUIRE(narrow.size_y() == 7 * CELL_BLOCKS);

    settings.cells_x = MAX_WORLD_CELLS + 50;
    settings.cells_y = 1;
    settings.centre_island = false;
    const Terrain wide = generate_terrain(settings);
    REQUIRE(wide.cells_x() == MAX_WORLD_CELLS);
    REQUIRE(wide.cells_y() == 1);
    // Asked for no island, so all ocean
    for (int32_t cx = 0; cx < wide.cells_x(); ++cx) REQUIRE(wide.is_ocean_cell(cx, 0));

    // Too small for any island to fit in any of its turns: all ocean
    settings.cells_x = 1;
    settings.centre_island = true;
    REQUIRE(generate_terrain(settings).is_ocean_cell(0, 0));

    // And no cells at all is no terrain, which is what a load starts from
    settings.cells_x = 0;
    REQUIRE(generate_terrain(settings).size_x() == 0);
}

TEST_CASE("Terrain: everything under the water is stone", "[sim][terrain][water]") {
    // Which blocks count: wholly under the water's top, level + 1
    REQUIRE(block_under_water(0, 3));         // block top 4, water top 4
    REQUIRE_FALSE(block_under_water(0, 2));   // water top 3, the block sticks out
    REQUIRE(block_under_water(3, DEFAULT_WATER_LEVEL));
    REQUIRE_FALSE(block_under_water(4, DEFAULT_WATER_LEVEL));
    REQUIRE(block_under_water(2, 11));
    REQUIRE_FALSE(block_under_water(2, 10));

    // A deep sea: the bottom seven layers of blocks are under it
    TerrainSettings settings;
    settings.water_level = 27;
    const Terrain terrain = generate_terrain(settings);
    bool ground_above = false;
    for (int32_t y = 0; y < terrain.size_y(); ++y) {
        for (int32_t x = 0; x < terrain.size_x(); ++x) {
            if (terrain.is_ocean_cell(x / CELL_BLOCKS, y / CELL_BLOCKS)) continue;
            // Every block there is under it, which is not to say every layer:
            // a column lower than the water is still only as tall as it is
            const int32_t top = terrain.column_height(x, y) - 1;
            for (int32_t z = 0; z < std::min(7, top + 1); ++z) {
                REQUIRE(terrain.get(x, y, z) == BlockType::Stone);
            }
            if (top >= 7 && terrain.get(x, y, top) != BlockType::Stone) ground_above = true;
        }
    }
    // The land above the water still has its grass, sand or snow
    REQUIRE(ground_above);

    // And a game starts with its water where its terrain was made for
    const Simulation sim(1, settings);
    REQUIRE(sim.water_level() == 27);
}

TEST_CASE("Terrain: the same settings give the same terrain", "[sim][terrain]") {
    const Terrain a = generate_terrain(TerrainSettings{});
    const Terrain b = generate_terrain(TerrainSettings{});
    REQUIRE(a == b);

    TerrainSettings other;
    other.seed = 42;
    REQUIRE_FALSE(generate_terrain(other) == a);
}

TEST_CASE("Terrain: written and read back", "[sim][terrain]") {
    const Terrain terrain = generate_terrain(TerrainSettings{});
    ByteWriter out;
    terrain.write(out);
    // The sizes, a byte a cell, and the four land cells' blocks
    REQUIRE(out.data().size() ==
            20 + DEFAULT_WORLD_CELLS * DEFAULT_WORLD_CELLS + 4 * CELL_BLOCKS * CELL_BLOCKS * CELL_BLOCKS);

    Terrain copy;
    ByteReader in(out.data());
    REQUIRE(copy.read(in));
    REQUIRE(in.at_end());
    REQUIRE(copy == terrain);

    // An empty terrain round trips too
    ByteWriter empty_out;
    Terrain{}.write(empty_out);
    Terrain empty_copy = Terrain::of_blocks(2, 2, 2);
    ByteReader empty_in(empty_out.data());
    REQUIRE(empty_copy.read(empty_in));
    REQUIRE(empty_copy == Terrain{});
}

TEST_CASE("Terrain: damaged bytes are refused", "[sim][terrain]") {
    // Four one column cells, two tall: the first is land, the rest ocean
    Terrain terrain = Terrain::of_blocks(2, 2, 2);
    terrain.set(0, 0, 0, BlockType::Grass);
    ByteWriter out;
    terrain.write(out);
    const std::vector<uint8_t> good = out.data();
    // Five sizes, then the first cell's kind and its two blocks
    REQUIRE(good.size() == 20 + 1 + 2 + 3);

    const auto refused = [](const std::vector<uint8_t>& bytes) {
        Terrain t;
        ByteReader in(bytes);
        return !t.read(in);
    };
    REQUIRE_FALSE(refused(good));

    // Not a block type
    std::vector<uint8_t> bad_type = good;
    bad_type[21] = BLOCK_TYPE_COUNT;
    REQUIRE(refused(bad_type));

    // A cell that is neither ocean nor land
    std::vector<uint8_t> bad_kind = good;
    bad_kind[20] = 2;
    REQUIRE(refused(bad_kind));

    // A negative size
    std::vector<uint8_t> negative = good;
    poke_u32(negative, 4, static_cast<uint32_t>(-2));
    REQUIRE(refused(negative));

    // Sized past anything real, refused before it is allocated
    std::vector<uint8_t> huge = good;
    poke_u32(huge, 0, 1u << 20);
    poke_u32(huge, 4, 1u << 20);
    REQUIRE(refused(huge));

    // Zero on one axis but not the others
    std::vector<uint8_t> flat = good;
    poke_u32(flat, 8, 0);
    REQUIRE(refused(flat));

    // A sea floor taller than the world
    std::vector<uint8_t> deep = good;
    poke_u32(deep, 16, 3);
    REQUIRE(refused(deep));

    // Fewer bytes than the cells say
    std::vector<uint8_t> short_bytes(good.begin(), good.end() - 1);
    REQUIRE(refused(short_bytes));
}

TEST_CASE("Terrain: part of the checksum, and of a save", "[sim][terrain][save]") {
    const Simulation a(1);
    const Simulation b(1);
    REQUIRE(a.terrain() == generate_terrain(TerrainSettings{}));
    REQUIRE(a.checksum() == b.checksum());

    TerrainSettings other;
    other.seed = 42;
    const Simulation c(1, other);
    REQUIRE(a.checksum() != c.checksum());

    // Saved and loaded, from a game whose terrain is not the default one, so
    // the load cannot have got it by generating its own
    const std::vector<uint8_t> bytes = write_save(c, CommandQueue{});
    const auto loaded = read_save(bytes);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->simulation.terrain() == c.terrain());
    REQUIRE(loaded->simulation.checksum() == c.checksum());
}

TEST_CASE("Save: a damaged terrain is refused", "[sim][terrain][save]") {
    std::vector<uint8_t> bytes = write_save(Simulation(1), CommandQueue{});
    const size_t section = find_section(bytes, SECTION_TERRAIN);
    REQUIRE(section != 0);

    // The first cell's kind, after the section header and the five sizes
    bytes[section + 12 + 20] = 0xFF;

    std::string error;
    REQUIRE_FALSE(read_save(bytes, &error).has_value());
    REQUIRE(error.find("TERR") != std::string::npos);
}
