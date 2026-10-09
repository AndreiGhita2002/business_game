// The simulation's terrain: the fixed point noise it is generated from, the
// block grid and its queries, what generation makes, and the terrain going
// into the checksum and through a save.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <set>

#include "sim/Noise.hpp"
#include "sim/Save.hpp"
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
    Terrain terrain(4, 3, 2);
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
    const Terrain empty(0, 5, 5);
    REQUIRE(empty.size_x() == 0);
    REQUIRE(empty.size_y() == 0);
    REQUIRE(empty.size_z() == 0);
}

TEST_CASE("Terrain: column heights and the ground under a point", "[sim][terrain]") {
    Terrain terrain(3, 2, 4);
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

TEST_CASE("Terrain: generated columns are stone, then dirt, then grass on top", "[sim][terrain]") {
    const TerrainSettings settings;
    const Terrain terrain = generate_terrain(settings);
    REQUIRE(terrain.size_x() == settings.size_x);
    REQUIRE(terrain.size_y() == settings.size_y);
    REQUIRE(terrain.size_z() == settings.size_z);

    std::set<int32_t> heights;
    std::set<BlockType> types;
    for (int32_t x = 0; x < terrain.size_x(); ++x) {
        for (int32_t y = 0; y < terrain.size_y(); ++y) {
            const int32_t height = terrain.column_height(x, y);
            heights.insert(height);
            // Never a hole through the world
            REQUIRE(height >= 1);

            const int32_t top = height - 1;
            for (int32_t z = 0; z < terrain.size_z(); ++z) {
                const BlockType block = terrain.get(x, y, z);
                types.insert(block);
                if (z > top) REQUIRE(block == BlockType::Air);
                else if (block_under_water(z, settings.water_level)) REQUIRE(block == BlockType::Stone);
                else if (z == top) REQUIRE(block == BlockType::Grass);
                else if (z >= top - settings.dirt_depth) REQUIRE(block == BlockType::Dirt);
                else REQUIRE(block == BlockType::Stone);
            }
        }
    }

    // The default map has hills, and so all three kinds of block
    REQUIRE(heights.size() > 1);
    REQUIRE(types.count(BlockType::Stone) == 1);
    REQUIRE(types.count(BlockType::Dirt) == 1);
    REQUIRE(types.count(BlockType::Grass) == 1);
}

TEST_CASE("Terrain: the hill height sets how tall and how steep", "[sim][terrain]") {
    // The tallest column, and the biggest step between two neighbours
    const auto measure = [](const Terrain& t, int32_t* top, int32_t* step) {
        *top = 0;
        *step = 0;
        for (int32_t x = 0; x < t.size_x(); ++x) {
            for (int32_t y = 0; y < t.size_y(); ++y) {
                const int32_t c = t.column_height(x, y);
                *top = std::max(*top, c);
                if (x + 1 < t.size_x()) *step = std::max(*step, std::abs(c - t.column_height(x + 1, y)));
                if (y + 1 < t.size_y()) *step = std::max(*step, std::abs(c - t.column_height(x, y + 1)));
            }
        }
    };

    TerrainSettings low;
    low.hill_height = 8;
    TerrainSettings high;
    high.hill_height = 32;
    int32_t low_top = 0, low_step = 0, high_top = 0, high_step = 0;
    measure(generate_terrain(low), &low_top, &low_step);
    measure(generate_terrain(high), &high_top, &high_step);

    // A sample stands at most hill_height blocks, and the noise stays under 1
    REQUIRE(low_top <= low.hill_height);
    REQUIRE(high_top <= high.hill_height);
    // The same hills, scaled: taller and steeper together
    REQUIRE(high_top > low_top);
    REQUIRE(high_step > low_step);

    // The height of the world is independent of it: hills taller than the
    // world are cut off at its top rather than poking through
    TerrainSettings capped;
    capped.size_z = 3;
    capped.hill_height = 64;
    const Terrain flat_top = generate_terrain(capped);
    int32_t capped_top = 0, capped_step = 0;
    measure(flat_top, &capped_top, &capped_step);
    REQUIRE(capped_top == 3);

    // And a hill height of 0 is a flat world one block deep
    TerrainSettings none;
    none.hill_height = 0;
    int32_t none_top = 0, none_step = 0;
    measure(generate_terrain(none), &none_top, &none_step);
    REQUIRE(none_top == 1);
    REQUIRE(none_step == 0);
}

TEST_CASE("Terrain: everything under the water is stone", "[sim][terrain][water]") {
    // Which blocks count: wholly under the water's top, level + 1
    REQUIRE(block_under_water(0, 3));         // block top 4, water top 4
    REQUIRE_FALSE(block_under_water(0, 2));   // water top 3, the block sticks out
    REQUIRE(block_under_water(0, DEFAULT_WATER_LEVEL));
    REQUIRE_FALSE(block_under_water(1, DEFAULT_WATER_LEVEL));
    REQUIRE(block_under_water(2, 11));
    REQUIRE_FALSE(block_under_water(2, 10));

    // A deep sea: the bottom three layers of blocks are under it
    TerrainSettings settings;
    settings.water_level = 12;
    const Terrain terrain = generate_terrain(settings);
    bool grass_above = false;
    for (int32_t x = 0; x < terrain.size_x(); ++x) {
        for (int32_t y = 0; y < terrain.size_y(); ++y) {
            // Every block there is under it, which is not to say every layer:
            // a column lower than the water is still only as tall as it is
            const int32_t top = terrain.column_height(x, y) - 1;
            for (int32_t z = 0; z < std::min(3, top + 1); ++z) {
                REQUIRE(terrain.get(x, y, z) == BlockType::Stone);
            }
            if (top >= 3 && terrain.get(x, y, top) == BlockType::Grass) grass_above = true;
        }
    }
    // The hills above the water still have their grass
    REQUIRE(grass_above);

    // With no water over even the lowest blocks, the floor is grass again
    TerrainSettings dry;
    dry.water_level = MIN_WATER_LEVEL;
    const Terrain dry_terrain = generate_terrain(dry);
    REQUIRE(dry_terrain.get(0, 0, dry_terrain.column_height(0, 0) - 1) == BlockType::Grass);

    // And a game starts with its water where its terrain was made for
    const Simulation sim(1, settings);
    REQUIRE(sim.water_level() == 12);
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

    Terrain copy;
    ByteReader in(out.data());
    REQUIRE(copy.read(in));
    REQUIRE(in.at_end());
    REQUIRE(copy == terrain);

    // An empty terrain round trips too
    ByteWriter empty_out;
    Terrain{}.write(empty_out);
    Terrain empty_copy(2, 2, 2);
    ByteReader empty_in(empty_out.data());
    REQUIRE(empty_copy.read(empty_in));
    REQUIRE(empty_copy == Terrain{});
}

TEST_CASE("Terrain: damaged bytes are refused", "[sim][terrain]") {
    Terrain terrain(2, 2, 2);
    terrain.set(0, 0, 0, BlockType::Grass);
    ByteWriter out;
    terrain.write(out);
    const std::vector<uint8_t> good = out.data();

    const auto refused = [](const std::vector<uint8_t>& bytes) {
        Terrain t;
        ByteReader in(bytes);
        return !t.read(in);
    };
    REQUIRE_FALSE(refused(good));

    // Not a block type
    std::vector<uint8_t> bad_type = good;
    bad_type[12] = BLOCK_TYPE_COUNT;
    REQUIRE(refused(bad_type));

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

    // Fewer blocks than the size says
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

    // The first block, after the section header and the three sizes
    bytes[section + 12 + 12] = 0xFF;

    std::string error;
    REQUIRE_FALSE(read_save(bytes, &error).has_value());
    REQUIRE(error.find("TERR") != std::string::npos);
}
