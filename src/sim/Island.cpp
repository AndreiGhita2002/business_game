//
// Created by Claude on 09.10.2026.
//

#include "sim/Island.hpp"

#include <algorithm>
#include <array>

#include "sim/Noise.hpp"
#include "sim/Rng.hpp"

namespace sim {

namespace {

/** How each elevation type is shaped, in blocks. */
struct Relief {
    // How far the island stands over its beach once the ramp is done, before
    // any noise
    int32_t lift;
    // How much the noise adds at most on top of that
    int32_t amplitude;
    int octaves;
    // Per block, of the lowest octave; each octave after it is twice as fine
    // and half as strong
    Fixed frequency;
    // Ridged noise (1 - |n|, squared) for ridgelines rather than round hills
    bool ridged;
    // The steepest step from a column to its neighbour
    int32_t max_step;
};

Relief relief_for(const Elevation elevation) {
    switch (elevation) {
        case Elevation::Flat: return Relief{1, 2, 2, Fixed::from_ratio(1, 48), false, 1};
        case Elevation::Hilly: return Relief{2, 12, 4, Fixed::from_ratio(1, 40), false, 1};
        case Elevation::Mountainous: return Relief{3, 36, 5, Fixed::from_ratio(1, 56), true, 2};
    }
    return Relief{1, 2, 2, Fixed::from_ratio(1, 48), false, 1};
}

// How far inland, past the coast, the beach runs flat before the relief
// starts, and how far again before it is at its full height. In blocks.
constexpr int32_t BEACH_WIDTH = 2;
constexpr int32_t RELIEF_RAMP = 16;

// The coast's wobble: how fine its noise is, per block, and where in the
// noise it is read, well away from where the relief is, so the two do not
// follow each other
constexpr Fixed COAST_FREQUENCY = Fixed::from_ratio(1, 28);
constexpr Fixed COAST_OFFSET_X = Fixed::from_ratio(1725, 100);
constexpr Fixed COAST_OFFSET_Y = Fixed::from_ratio(4150, 100);

// Heights over the shore, in blocks, at which a grassland goes to bare rock
// and then gets snow on top, and at which a desert and a snowy island turn
// to rock under their top
constexpr int32_t ROCK_LINE = 18;
constexpr int32_t SNOW_LINE = 26;
constexpr int32_t HIGH_LINE = 16;

// The chamfer distance's steps: 5 straight and 7 diagonal is within a few
// percent of the true distance, times 5
constexpr int32_t STRAIGHT_STEP = 5;
constexpr int32_t DIAGONAL_STEP = 7;
constexpr int32_t FAR = INT32_MAX / 2;

Fixed abs_fixed(const Fixed v) { return v < Fixed{} ? -v : v; }

/**
 * Summed octaves of `noise` at block (x, y), each read at the middle of its
 * column, brought into [0, 1]. Ridged octaves are 1 - 2|n| squared, which is
 * 1 along the noise's zero lines and falls away either side.
 */
Fixed octave_noise(const PerlinNoise& noise, const int32_t x, const int32_t y, const Relief& relief,
                   const bool ridged) {
    const Fixed half = Fixed::from_ratio(1, 2);
    const Fixed one = Fixed::from_int(1);
    const Fixed bx = Fixed::from_int(x) + half;
    const Fixed by = Fixed::from_int(y) + half;

    Fixed total{};
    Fixed weights{};
    Fixed frequency = relief.frequency;
    Fixed weight = one;
    for (int o = 0; o < relief.octaves; ++o) {
        const Fixed n = noise.noise2d(bx * frequency, by * frequency);
        Fixed v;
        if (ridged) {
            // The noise rarely goes past a half either way, so 2|n| is
            // roughly [0, 1]
            v = std::clamp(one - abs_fixed(n) * 2, Fixed{}, one);
            v = v * v;
        } else {
            v = n;
        }
        total += v * weight;
        weights += weight;
        frequency = frequency * 2;
        weight = weight / 2;
    }
    const Fixed mean = mul_div(total, one, weights);
    if (ridged) return mean;
    // Summed octaves spread less than one does, so this is stretched a
    // little before it is moved into [0, 1]
    return std::clamp(mean * 3 / 2 + half, Fixed{}, one);
}

/** The block at height z of a column `top` blocks tall, `shore` being the lowest column that stands out of the water. */
BlockType column_block(const Biome biome, const int32_t z, const int32_t top, const int32_t shore,
                       const int32_t water_level) {
    if (block_under_water(z, water_level)) return BlockType::Stone;

    // How far down from the top block, which is 0
    const int32_t depth = top - 1 - z;
    const int32_t over_shore = top - shore;
    const bool beach = over_shore <= 0;

    switch (biome) {
        case Biome::Grassland:
            if (beach) return depth < 3 ? BlockType::Sand : BlockType::Stone;
            if (over_shore >= SNOW_LINE) return depth == 0 ? BlockType::Snow : BlockType::Stone;
            if (over_shore >= ROCK_LINE) return BlockType::Stone;
            if (depth == 0) return BlockType::Grass;
            return depth < 3 ? BlockType::Dirt : BlockType::Stone;

        case Biome::Desert:
            if (over_shore >= HIGH_LINE) return depth < 4 ? BlockType::Sandstone : BlockType::Stone;
            if (depth < 3) return BlockType::Sand;
            return depth < 7 ? BlockType::Sandstone : BlockType::Stone;

        case Biome::Snowy:
            if (beach) return depth < 2 ? BlockType::Gravel : BlockType::Stone;
            if (depth == 0) return BlockType::Snow;
            if (over_shore >= HIGH_LINE) return BlockType::Stone;
            return depth < 3 ? BlockType::Dirt : BlockType::Stone;
    }
    return BlockType::Stone;
}

} // namespace

const char* island_shape_name(const IslandShape shape) {
    switch (shape) {
        case IslandShape::Square: return "square";
        case IslandShape::Line: return "line";
        case IslandShape::L: return "L";
        case IslandShape::T: return "T";
        case IslandShape::Zigzag: return "zigzag";
    }
    return "?";
}

const char* elevation_name(const Elevation elevation) {
    switch (elevation) {
        case Elevation::Flat: return "flat";
        case Elevation::Hilly: return "hilly";
        case Elevation::Mountainous: return "mountainous";
    }
    return "?";
}

const char* biome_name(const Biome biome) {
    switch (biome) {
        case Biome::Grassland: return "grassland";
        case Biome::Desert: return "desert";
        case Biome::Snowy: return "snowy";
    }
    return "?";
}

bool Footprint::contains(const int32_t x, const int32_t y) const {
    return std::find(cells.begin(), cells.end(), CellPos{x, y}) != cells.end();
}

Footprint island_footprint(const IslandShape shape, const uint8_t rotation) {
    // Each shape unturned, as four cells
    std::array<CellPos, 4> base;
    switch (shape) {
        case IslandShape::Square: base = {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}}; break;
        case IslandShape::Line: base = {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}}; break;
        case IslandShape::L: base = {{{0, 0}, {0, 1}, {0, 2}, {1, 2}}}; break;
        case IslandShape::T: base = {{{0, 0}, {1, 0}, {2, 0}, {1, 1}}}; break;
        case IslandShape::Zigzag: base = {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}}; break;
    }

    // A quarter turn is (x, y) to (-y, x)
    for (int turn = 0; turn < rotation % 4; ++turn) {
        for (CellPos& c : base) c = CellPos{-c.y, c.x};
    }

    int32_t min_x = base[0].x, min_y = base[0].y, max_x = base[0].x, max_y = base[0].y;
    for (const CellPos& c : base) {
        min_x = std::min(min_x, c.x);
        min_y = std::min(min_y, c.y);
        max_x = std::max(max_x, c.x);
        max_y = std::max(max_y, c.y);
    }

    Footprint footprint;
    for (const CellPos& c : base) footprint.cells.push_back(CellPos{c.x - min_x, c.y - min_y});
    std::sort(footprint.cells.begin(), footprint.cells.end(), [](const CellPos& a, const CellPos& b) {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    });
    footprint.width = max_x - min_x + 1;
    footprint.height = max_y - min_y + 1;
    return footprint;
}

IslandSpec random_island_spec(const uint32_t seed) {
    Rng rng(seed);
    IslandSpec spec;
    spec.shape = static_cast<IslandShape>(rng.next_below(ISLAND_SHAPE_COUNT));
    spec.rotation = static_cast<uint8_t>(rng.next_below(4));
    spec.elevation = static_cast<Elevation>(rng.next_below(ELEVATION_COUNT));
    spec.biome = static_cast<Biome>(rng.next_below(BIOME_COUNT));
    spec.seed = rng.next_u32();
    return spec;
}

bool centred_island_cell(const Terrain& terrain, const IslandSpec& spec, CellPos* out) {
    const Footprint footprint = island_footprint(spec.shape, spec.rotation);
    if (footprint.width > terrain.cells_x() || footprint.height > terrain.cells_y()) return false;
    *out = CellPos{(terrain.cells_x() - footprint.width) / 2, (terrain.cells_y() - footprint.height) / 2};
    return true;
}

bool place_island(Terrain& terrain, const int32_t cell_x, const int32_t cell_y, const IslandSpec& spec,
                  const int32_t water_level) {
    if (terrain.cell_blocks() < CELL_BLOCKS) return false;

    const Footprint footprint = island_footprint(spec.shape, spec.rotation);
    for (const CellPos& c : footprint.cells) {
        if (!terrain.is_ocean_cell(cell_x + c.x, cell_y + c.y)) return false;
    }

    const int32_t cell = terrain.cell_blocks();
    const int32_t width = footprint.width * cell;
    const int32_t height = footprint.height * cell;
    const auto index = [width](const int32_t x, const int32_t y) {
        return static_cast<size_t>(x) + static_cast<size_t>(y) * static_cast<size_t>(width);
    };
    const auto in_box = [width, height](const int32_t x, const int32_t y) {
        return x >= 0 && x < width && y >= 0 && y < height;
    };

    // Which columns are on the island's cells at all
    std::vector<uint8_t> inside(static_cast<size_t>(width) * height);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            inside[index(x, y)] = footprint.contains(x / cell, y / cell) ? 1 : 0;
        }
    }

    // 2. How far each column is from the nearest one off the footprint,
    // times STRAIGHT_STEP. Off the box counts as off the footprint.
    std::vector<int32_t> distance(inside.size());
    for (size_t i = 0; i < inside.size(); ++i) distance[i] = inside[i] ? FAR : 0;
    const auto distance_at = [&](const int32_t x, const int32_t y) {
        return in_box(x, y) ? distance[index(x, y)] : 0;
    };
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            int32_t& d = distance[index(x, y)];
            if (d == 0) continue;
            d = std::min({d, distance_at(x - 1, y) + STRAIGHT_STEP, distance_at(x, y - 1) + STRAIGHT_STEP,
                          distance_at(x - 1, y - 1) + DIAGONAL_STEP, distance_at(x + 1, y - 1) + DIAGONAL_STEP});
        }
    }
    for (int32_t y = height - 1; y >= 0; --y) {
        for (int32_t x = width - 1; x >= 0; --x) {
            int32_t& d = distance[index(x, y)];
            if (d == 0) continue;
            d = std::min({d, distance_at(x + 1, y) + STRAIGHT_STEP, distance_at(x, y + 1) + STRAIGHT_STEP,
                          distance_at(x + 1, y + 1) + DIAGONAL_STEP, distance_at(x - 1, y + 1) + DIAGONAL_STEP});
        }
    }

    // The lowest column that stands out of the water, and the sea floor
    int32_t shore = 1;
    while (shore < terrain.size_z() && block_under_water(shore - 1, water_level)) shore++;
    const int32_t sea_floor = terrain.sea_floor();

    const Relief relief = relief_for(spec.elevation);
    const PerlinNoise noise(spec.seed);
    const Fixed one = Fixed::from_int(1);

    // 3 and 4. The coast and the relief, as a column height each
    std::vector<int32_t> tops(inside.size(), sea_floor);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            const int32_t d = distance[index(x, y)];
            if (d == 0) continue;

            // Pinned, so every island meets the ocean round it at the floor
            if (d <= ISLAND_EDGE_FLOOR * STRAIGHT_STEP) continue;

            const Fixed coast_x = (Fixed::from_int(x) + Fixed::from_ratio(1, 2)) * COAST_FREQUENCY;
            const Fixed coast_y = (Fixed::from_int(y) + Fixed::from_ratio(1, 2)) * COAST_FREQUENCY;
            const Fixed wobble = std::clamp(noise.noise2d(coast_x + COAST_OFFSET_X, coast_y + COAST_OFFSET_Y) * 2,
                                            -one, one) * ISLAND_COAST_WOBBLE;
            // How far inland of the coast, in blocks: below 0 is sea
            const Fixed inland = Fixed::from_ratio(d, STRAIGHT_STEP) - Fixed::from_int(ISLAND_COAST_MARGIN) + wobble;

            Fixed top;
            if (inland <= Fixed{}) {
                // Just under the water at the coast, and down a block for
                // every two out from it
                top = Fixed::from_int(shore - 1) + inland / 2;
            } else {
                const Fixed ramp = std::clamp((inland - Fixed::from_int(BEACH_WIDTH)) / RELIEF_RAMP, Fixed{}, one);
                Fixed n = octave_noise(noise, x, y, relief, relief.ridged);
                // Ridges alone run in lines from end to end; rolling them
                // over a smoother field gives peaks and passes along them
                if (relief.ridged) {
                    const Fixed rolling = octave_noise(noise, x, y, relief, false);
                    n = n * (Fixed::from_ratio(1, 2) + rolling / 2);
                }
                top = Fixed::from_int(shore) + ramp * (Fixed::from_int(relief.lift) + n * relief.amplitude);
            }
            tops[index(x, y)] = static_cast<int32_t>(
                std::clamp<int64_t>(top.floor_int(), sea_floor, terrain.size_z()));
        }
    }

    // 5. No column more than max_step over any of its four neighbours, off
    // the box being the sea floor. Only ever lowers: a forward and a backward
    // pass is the exact answer for four neighbours.
    const auto top_at = [&](const int32_t x, const int32_t y) {
        return in_box(x, y) ? tops[index(x, y)] : sea_floor;
    };
    const int32_t step = relief.max_step;
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            int32_t& t = tops[index(x, y)];
            t = std::min({t, top_at(x - 1, y) + step, top_at(x, y - 1) + step});
        }
    }
    for (int32_t y = height - 1; y >= 0; --y) {
        for (int32_t x = width - 1; x >= 0; --x) {
            int32_t& t = tops[index(x, y)];
            t = std::min({t, top_at(x + 1, y) + step, top_at(x, y + 1) + step});
        }
    }

    // 6. The blocks. The footprint's cells start again as ocean, so only the
    // blocks that differ from it need writing.
    for (const CellPos& c : footprint.cells) terrain.reset_cell(cell_x + c.x, cell_y + c.y);
    const int32_t origin_x = cell_x * cell;
    const int32_t origin_y = cell_y * cell;
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            if (!inside[index(x, y)]) continue;
            const int32_t top = tops[index(x, y)];
            for (int32_t z = 0; z < top; ++z) {
                terrain.set(origin_x + x, origin_y + y, z, column_block(spec.biome, z, top, shore, water_level));
            }
        }
    }
    return true;
}

} // namespace sim
