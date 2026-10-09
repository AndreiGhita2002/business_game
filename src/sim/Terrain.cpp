//
// Created by Claude on 09.10.2026.
//

#include "sim/Terrain.hpp"

#include <algorithm>

#include "sim/Noise.hpp"

namespace sim {

const char* block_type_name(const BlockType type) {
    switch (type) {
        case BlockType::Air: return "air";
        case BlockType::Stone: return "stone";
        case BlockType::Dirt: return "dirt";
        case BlockType::Grass: return "grass";
    }
    return "?";
}

Terrain::Terrain(const int32_t size_x, const int32_t size_y, const int32_t size_z) {
    if (size_x <= 0 || size_y <= 0 || size_z <= 0) return;
    sx = size_x;
    sy = size_y;
    sz = size_z;
    blocks.assign(static_cast<size_t>(sx) * sy * sz, BlockType::Air);
}

bool Terrain::in_bounds(const int32_t x, const int32_t y, const int32_t z) const {
    return x >= 0 && x < sx && y >= 0 && y < sy && z >= 0 && z < sz;
}

BlockType Terrain::get(const int32_t x, const int32_t y, const int32_t z) const {
    if (!in_bounds(x, y, z)) return BlockType::Air;
    return blocks[index(x, y, z)];
}

bool Terrain::set(const int32_t x, const int32_t y, const int32_t z, const BlockType type) {
    if (!in_bounds(x, y, z)) return false;
    blocks[index(x, y, z)] = type;
    return true;
}

int32_t Terrain::column_height(const int32_t x, const int32_t y) const {
    for (int32_t z = sz - 1; z >= 0; --z) {
        if (is_solid(x, y, z)) return z + 1;
    }
    return 0;
}

Fixed Terrain::ground_level(const Fixed x, const Fixed y) const {
    // Rounded down both times, so a point just west of the origin lands in
    // column -1 rather than column 0
    const auto column = [](const Fixed v) {
        const int64_t unit = v.floor_int();
        return unit >= 0 ? unit / BLOCK_SIZE : -((-unit + BLOCK_SIZE - 1) / BLOCK_SIZE);
    };
    const int64_t column_x = column(x);
    const int64_t column_y = column(y);
    if (column_x < 0 || column_x >= sx || column_y < 0 || column_y >= sy) return Fixed{};
    return Fixed::from_int(int64_t{column_height(static_cast<int32_t>(column_x),
                                                 static_cast<int32_t>(column_y))} * BLOCK_SIZE);
}

void Terrain::write(ByteWriter& out) const {
    out.write_i32(sx);
    out.write_i32(sy);
    out.write_i32(sz);
    for (const BlockType block : blocks) out.write_u8(static_cast<uint8_t>(block));
}

bool Terrain::read(ByteReader& in) {
    int32_t x = 0, y = 0, z = 0;
    if (!in.read_i32(&x) || !in.read_i32(&y) || !in.read_i32(&z)) return false;
    if (x < 0 || y < 0 || z < 0) return false;

    // Checked before anything is allocated, so a damaged size cannot ask for
    // more memory than any terrain would use
    const int64_t count = int64_t{x} * y * z;
    if (count > MAX_TERRAIN_BLOCKS) return false;
    // A terrain is either empty or has blocks on every axis
    if (count == 0 && (x != 0 || y != 0 || z != 0)) return false;

    std::span<const uint8_t> bytes;
    if (!in.read_bytes(&bytes, static_cast<size_t>(count))) return false;

    std::vector<BlockType> read_blocks;
    read_blocks.reserve(bytes.size());
    for (const uint8_t b : bytes) {
        if (b >= BLOCK_TYPE_COUNT) return false;
        read_blocks.push_back(static_cast<BlockType>(b));
    }

    sx = x;
    sy = y;
    sz = z;
    blocks = std::move(read_blocks);
    return true;
}

Terrain generate_terrain(const TerrainSettings& settings) {
    Terrain terrain(settings.size_x, settings.size_y, settings.size_z);
    if (terrain.size_x() == 0) return terrain;

    const PerlinNoise noise(settings.seed);
    const Fixed half = Fixed::from_ratio(1, 2);

    for (int32_t y = 0; y < settings.size_y; ++y) {
        for (int32_t x = 0; x < settings.size_x; ++x) {
            const Fixed sample = noise.noise2d((Fixed::from_int(x) + half) * settings.noise_scale,
                                               (Fixed::from_int(y) + half) * settings.noise_scale);

            // A sample of 1 would reach the top of the world, the way the old
            // voxel terrain scaled it to the height of its chunk. The noise
            // rarely goes past a half either way, and everything below 0 is
            // the lowest ground there is.
            const int32_t top = static_cast<int32_t>(std::clamp<int64_t>(
                (sample * settings.size_z).floor_int(), 0, settings.size_z - 1));

            for (int32_t z = 0; z <= top; ++z) {
                BlockType type = BlockType::Stone;
                if (z == top) type = BlockType::Grass;
                else if (z >= top - settings.dirt_depth) type = BlockType::Dirt;
                terrain.set(x, y, z, type);
            }
        }
    }
    return terrain;
}

} // namespace sim
