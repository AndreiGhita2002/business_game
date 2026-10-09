//
// Created by Claude on 09.10.2026.
//

#include "sim/Terrain.hpp"

#include <algorithm>

#include "sim/Island.hpp"

namespace sim {

const char* block_type_name(const BlockType type) {
    switch (type) {
        case BlockType::Air: return "air";
        case BlockType::Stone: return "stone";
        case BlockType::Dirt: return "dirt";
        case BlockType::Grass: return "grass";
        case BlockType::Sand: return "sand";
        case BlockType::Sandstone: return "sandstone";
        case BlockType::Snow: return "snow";
        case BlockType::Gravel: return "gravel";
    }
    return "?";
}

Terrain::Terrain(const int32_t cells_x, const int32_t cells_y, const int32_t cell_blocks,
                 const int32_t size_z, const int32_t sea_floor) {
    if (cells_x <= 0 || cells_y <= 0 || cell_blocks <= 0 || size_z <= 0) return;
    if (sea_floor < 0 || sea_floor > size_z) return;
    cx = cells_x;
    cy = cells_y;
    cb = cell_blocks;
    sz = size_z;
    floor_blocks = sea_floor;
    cells.resize(static_cast<size_t>(cx) * cy);
}

Terrain Terrain::of_blocks(const int32_t size_x, const int32_t size_y, const int32_t size_z) {
    return Terrain(size_x, size_y, 1, size_z, 0);
}

bool Terrain::in_bounds(const int32_t x, const int32_t y, const int32_t z) const {
    return x >= 0 && x < size_x() && y >= 0 && y < size_y() && z >= 0 && z < sz;
}

bool Terrain::is_ocean_cell(const int32_t cell_x, const int32_t cell_y) const {
    if (cell_x < 0 || cell_x >= cx || cell_y < 0 || cell_y >= cy) return false;
    return cells[cell_index(cell_x, cell_y)].empty();
}

void Terrain::reset_cell(const int32_t cell_x, const int32_t cell_y) {
    if (cell_x < 0 || cell_x >= cx || cell_y < 0 || cell_y >= cy) return;
    // Swapped with an empty one rather than cleared, so the memory goes too
    std::vector<BlockType>().swap(cells[cell_index(cell_x, cell_y)]);
}

size_t Terrain::block_index(const int32_t x, const int32_t y, const int32_t z) const {
    const auto lx = static_cast<size_t>(x % cb);
    const auto ly = static_cast<size_t>(y % cb);
    const auto side = static_cast<size_t>(cb);
    return lx + ly * side + static_cast<size_t>(z) * side * side;
}

BlockType Terrain::get(const int32_t x, const int32_t y, const int32_t z) const {
    if (!in_bounds(x, y, z)) return BlockType::Air;
    const std::vector<BlockType>& cell = cells[cell_index(x / cb, y / cb)];
    if (cell.empty()) return ocean_block(z);
    return cell[block_index(x, y, z)];
}

bool Terrain::set(const int32_t x, const int32_t y, const int32_t z, const BlockType type) {
    if (!in_bounds(x, y, z)) return false;
    std::vector<BlockType>& cell = cells[cell_index(x / cb, y / cb)];
    if (cell.empty()) {
        // Ocean already holds it: no need for blocks of its own
        if (ocean_block(z) == type) return true;

        // The sea floor first, so the rest of the cell reads as it did
        const auto side = static_cast<size_t>(cb);
        cell.assign(side * side * static_cast<size_t>(sz), BlockType::Air);
        std::fill_n(cell.begin(), side * side * static_cast<size_t>(floor_blocks), BlockType::Stone);
    }
    cell[block_index(x, y, z)] = type;
    return true;
}

int32_t Terrain::column_height(const int32_t x, const int32_t y) const {
    if (!in_bounds(x, y, 0)) return 0;
    if (cells[cell_index(x / cb, y / cb)].empty()) return floor_blocks;
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
    if (column_x < 0 || column_x >= size_x() || column_y < 0 || column_y >= size_y()) return Fixed{};
    return Fixed::from_int(int64_t{column_height(static_cast<int32_t>(column_x),
                                                 static_cast<int32_t>(column_y))} * BLOCK_SIZE);
}

void Terrain::write(ByteWriter& out) const {
    out.write_i32(cx);
    out.write_i32(cy);
    out.write_i32(cb);
    out.write_i32(sz);
    out.write_i32(floor_blocks);
    for (const std::vector<BlockType>& cell : cells) {
        out.write_u8(cell.empty() ? 0 : 1);
        // A block is a byte (BlockType is a uint8_t), so a cell goes out in
        // one copy: the checksum writes the whole state every time it is
        // taken, and a cell is a quarter of a megabyte
        static_assert(sizeof(BlockType) == 1);
        out.write_bytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(cell.data()), cell.size()));
    }
}

bool Terrain::read(ByteReader& in) {
    int32_t cells_x = 0, cells_y = 0, cell_blocks = 0, size_z = 0, sea_floor = 0;
    if (!in.read_i32(&cells_x) || !in.read_i32(&cells_y) || !in.read_i32(&cell_blocks) ||
        !in.read_i32(&size_z) || !in.read_i32(&sea_floor))
        return false;

    // Checked before anything is allocated, so a damaged size cannot ask for
    // more memory than any terrain would use
    if (cells_x < 0 || cells_y < 0 || cell_blocks < 0 || size_z < 0) return false;
    if (cells_x > MAX_TERRAIN_CELLS_PER_SIDE || cells_y > MAX_TERRAIN_CELLS_PER_SIDE ||
        cell_blocks > MAX_TERRAIN_CELL_BLOCKS || size_z > MAX_TERRAIN_CELL_BLOCKS)
        return false;
    // A terrain is either empty or has blocks on every axis
    const bool empty = cells_x == 0 || cells_y == 0 || cell_blocks == 0 || size_z == 0;
    if (empty && (cells_x != 0 || cells_y != 0 || cell_blocks != 0 || size_z != 0 || sea_floor != 0))
        return false;
    if (sea_floor < 0 || sea_floor > size_z) return false;

    const int64_t cell_size = int64_t{cell_blocks} * cell_blocks * size_z;
    const int64_t cell_count = int64_t{cells_x} * cells_y;
    std::vector<std::vector<BlockType>> read_cells(static_cast<size_t>(cell_count));
    int64_t land_blocks = 0;

    for (std::vector<BlockType>& cell : read_cells) {
        uint8_t kind = 0;
        if (!in.read_u8(&kind)) return false;
        if (kind == 0) continue;
        if (kind != 1) return false;

        land_blocks += cell_size;
        if (land_blocks > MAX_TERRAIN_BLOCKS) return false;

        std::span<const uint8_t> bytes;
        if (!in.read_bytes(&bytes, static_cast<size_t>(cell_size))) return false;
        cell.reserve(bytes.size());
        for (const uint8_t b : bytes) {
            if (b >= BLOCK_TYPE_COUNT) return false;
            cell.push_back(static_cast<BlockType>(b));
        }
    }

    cx = cells_x;
    cy = cells_y;
    cb = cell_blocks;
    sz = size_z;
    floor_blocks = sea_floor;
    cells = std::move(read_cells);
    return true;
}

Terrain generate_terrain(const TerrainSettings& settings) {
    if (settings.cells_x <= 0 || settings.cells_y <= 0) return Terrain{};

    Terrain terrain(std::min(settings.cells_x, MAX_WORLD_CELLS), std::min(settings.cells_y, MAX_WORLD_CELLS));
    if (!settings.centre_island) return terrain;

    // The island's own turn first, then the others, so a world too narrow
    // for a line one way round can still take it the other
    IslandSpec spec = random_island_spec(settings.seed);
    for (int turn = 0; turn < 4; ++turn) {
        CellPos at;
        if (centred_island_cell(terrain, spec, &at)) {
            place_island(terrain, at.x, at.y, spec, settings.water_level);
            break;
        }
        spec.rotation = static_cast<uint8_t>((spec.rotation + 1) % 4);
    }
    return terrain;
}

} // namespace sim
