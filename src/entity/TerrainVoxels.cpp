//
// Created by Claude on 09.10.2026.
//

#include "entity/TerrainVoxels.hpp"

#include <algorithm>
#include <raylib.h>

#include "voxel/VoxelMap.hpp"

namespace {

constexpr int LAST = BLOCK_VOXELS - 1;

struct Side {
    int dx;
    int dy;
    uint8_t bit;
};

constexpr Side SIDES[4] = {
    {1, 0, SIDE_X_POS},
    {-1, 0, SIDE_X_NEG},
    {0, 1, SIDE_Y_POS},
    {0, -1, SIDE_Y_NEG},
};

/** Whether a voxel is in the outer row of the block on any of the sides in `mask`. */
bool on_side(const uint8_t mask, const Int3 v) {
    return ((mask & SIDE_X_POS) && v.x == LAST) || ((mask & SIDE_X_NEG) && v.x == 0) ||
           ((mask & SIDE_Y_POS) && v.y == LAST) || ((mask & SIDE_Y_NEG) && v.y == 0);
}

/** Whether a lowered edge has taken this voxel out of a solid block. */
bool lowered_away(const BlockDetail& detail, const Int3 v) {
    return v.z == LAST && on_side(detail.lowered, v);
}

/** A block type's voxel where it is the top of its column, which is the one grass turns green. */
VoxelID top_voxel(const sim::BlockType type) {
    switch (type) {
        case sim::BlockType::Air: return 0;
        case sim::BlockType::Stone: return STONE_VOXEL;
        case sim::BlockType::Dirt: return DIRT_VOXEL;
        case sim::BlockType::Grass: return GRASS_VOXEL;
        case sim::BlockType::Sand: return SAND_VOXEL;
        case sim::BlockType::Sandstone: return SANDSTONE_VOXEL;
        case sim::BlockType::Snow: return SNOW_VOXEL;
        case sim::BlockType::Gravel: return GRAVEL_VOXEL;
    }
    return 0;
}

} // namespace

BlockDetail block_detail(const sim::Terrain& terrain, const int32_t x, const int32_t y, const int32_t z) {
    BlockDetail detail;

    if (terrain.is_solid(x, y, z)) {
        // Only an open top has an edge to lower: under another block it would
        // cut a groove into the side of a cliff
        if (terrain.is_solid(x, y, z + 1)) return detail;
        for (const Side& side : SIDES) {
            if (!terrain.is_solid(x + side.dx, y + side.dy, z)) detail.lowered |= side.bit;
        }
        return detail;
    }

#if TERRAIN_TRIMS
    // Air: a trim needs a floor to stand on and a wall to lean on
    const sim::BlockType below = terrain.get(x, y, z - 1);
    if (below == sim::BlockType::Air) return detail;
    for (const Side& side : SIDES) {
        if (terrain.is_solid(x + side.dx, y + side.dy, z)) detail.trims |= side.bit;
    }
    if (detail.trims != 0) {
        detail.trim_type = below;
        detail.floor_lowered = block_detail(terrain, x, y, z - 1).lowered;
    }
#endif
    // Air with no trims is drawn as nothing at all
    return detail;
}

VoxelID block_voxel(const sim::BlockType type, const BlockDetail& detail, const Int3 in_block) {
    if (type == sim::BlockType::Air) {
        // A trim is one voxel tall, so its top is open and it is drawn as the
        // floor's top: a grass floor runs green up to the wall
        if (in_block.z != 0 || !on_side(detail.trims, in_block)) return 0;
        // Nothing under it where the floor's edge was lowered
        BlockDetail floor;
        floor.lowered = detail.floor_lowered;
        if (lowered_away(floor, Int3{in_block.x, in_block.y, LAST})) return 0;
        return top_voxel(detail.trim_type);
    }

    if (lowered_away(detail, in_block)) return 0;

    switch (type) {
        case sim::BlockType::Grass: {
            // Green on the top voxel of each column, which is one lower under
            // a lowered edge
            const bool top = in_block.z == LAST ||
                (in_block.z == LAST - 1 && lowered_away(detail, Int3{in_block.x, in_block.y, LAST}));
            return top ? GRASS_VOXEL : DIRT_VOXEL;
        }
        default:
            // The same colour all through
            return top_voxel(type);
    }
}

Int3 terrain_voxel_size(const sim::Terrain& terrain) {
    return Int3{terrain.size_x() * BLOCK_VOXELS, terrain.size_y() * BLOCK_VOXELS,
                terrain.size_z() * BLOCK_VOXELS};
}

bool draw_terrain_cell(VoxelMap& map, const sim::Terrain& terrain, const int32_t cell_x, const int32_t cell_y) {
    if (terrain.is_ocean_cell(cell_x, cell_y)) return true;

    // Written straight into the chunks rather than through set_voxel(), which
    // would mark the neighbouring chunks of every voxel for remeshing one by
    // one. A new chunk is marked by ensure_chunk(), and the chunks round the
    // cell's edges are marked at the end.
    bool all_fit = true;
    const int32_t cell = terrain.cell_blocks();
    VoxelChunk* last_chunk = nullptr;
    Int3 last_chunk_pos{0, 0, 0};
    for (int32_t by = cell_y * cell; by < (cell_y + 1) * cell; ++by) {
        for (int32_t bx = cell_x * cell; bx < (cell_x + 1) * cell; ++bx) {
            // Up to the air block on top of the column, which is drawn
            // only for its trims. Nothing above that has anything to draw.
            const int32_t top = std::min(terrain.column_height(bx, by) + 1, terrain.size_z());
            for (int32_t bz = 0; bz < top; ++bz) {
                const sim::BlockType type = terrain.get(bx, by, bz);
                const BlockDetail detail = block_detail(terrain, bx, by, bz);
                // Air is only drawn for its trims
                if (type == sim::BlockType::Air && detail.trims == 0) continue;

                const Int3 base{bx * BLOCK_VOXELS, by * BLOCK_VOXELS, bz * BLOCK_VOXELS};
                // in_bounds first: get_voxel wraps an out of range
                // coordinate into a chunk rather than refusing it
                if (!map.in_bounds(base)) {
                    all_fit = false;
                    continue;
                }
                // The whole block is in this one chunk. Blocks come up a
                // column at a time, four to a chunk, so the last chunk
                // is kept rather than looked up in the map again; a
                // map's chunks never move once made.
                const Int3 chunk_pos{base.x / CHUNK_SIZE, base.y / CHUNK_SIZE, base.z / CHUNK_SIZE};
                if (last_chunk == nullptr || !(chunk_pos == last_chunk_pos)) {
                    last_chunk = &map.ensure_chunk(chunk_pos);
                    last_chunk_pos = chunk_pos;
                }
                VoxelChunk& chunk = *last_chunk;

                // A plain block wholly inside the map, which is nearly
                // every one, is one colour all through: a row of voxels
                // at a time instead of a voxel at a time
                const Int3 far{base.x + LAST, base.y + LAST, base.z + LAST};
                if (detail == BlockDetail{} && type != sim::BlockType::Grass && map.in_bounds(far)) {
                    const VoxelID id = block_voxel(type, detail, Int3{0, 0, 0});
                    for (int vz = 0; vz < BLOCK_VOXELS; ++vz) {
                        for (int vy = 0; vy < BLOCK_VOXELS; ++vy) {
                            VoxelID* row = VoxelMap::get_chunk_voxel(chunk, Int3{
                                base.x % CHUNK_SIZE, (base.y + vy) % CHUNK_SIZE, (base.z + vz) % CHUNK_SIZE});
                            std::fill_n(row, BLOCK_VOXELS, id);
                        }
                    }
                    continue;
                }

                for (int vz = 0; vz < BLOCK_VOXELS; ++vz) {
                    for (int vy = 0; vy < BLOCK_VOXELS; ++vy) {
                        for (int vx = 0; vx < BLOCK_VOXELS; ++vx) {
                            const Int3 grid_pos{base.x + vx, base.y + vy, base.z + vz};
                            if (!map.in_bounds(grid_pos)) {
                                all_fit = false;
                                continue;
                            }
                            *VoxelMap::get_chunk_voxel(chunk, Int3{
                                grid_pos.x % CHUNK_SIZE,
                                grid_pos.y % CHUNK_SIZE,
                                grid_pos.z % CHUNK_SIZE,
                            }) = block_voxel(type, detail, Int3{vx, vy, vz});
                        }
                    }
                }
            }
        }
    }

    // The chunks already standing next to the cell had faces towards it,
    // drawn while it was ocean, which its own chunks may now hide
    const int chunks_per_cell = cell * BLOCK_VOXELS / CHUNK_SIZE;
    const Int3 lo{cell_x * chunks_per_cell - 1, cell_y * chunks_per_cell - 1, 0};
    const Int3 hi{(cell_x + 1) * chunks_per_cell, (cell_y + 1) * chunks_per_cell, map.get_chunk_count().z - 1};
    map.mark_for_remesh(lo, hi);
    return all_fit;
}

bool build_terrain_voxels(VoxelMap& map, const sim::Terrain& terrain) {
    // Everything goes, so a terrain with fewer blocks than the last one (a
    // loaded game) leaves no stale ground behind
    map.clear();

    bool all_fit = true;
    for (int32_t cell_y = 0; cell_y < terrain.cells_y(); ++cell_y) {
        for (int32_t cell_x = 0; cell_x < terrain.cells_x(); ++cell_x) {
            if (!draw_terrain_cell(map, terrain, cell_x, cell_y)) all_fit = false;
        }
    }

    if (!all_fit) {
        const Int3 needed = terrain_voxel_size(terrain);
        const Int2 size = map.get_size();
        TraceLog(LOG_WARNING, "TERRAIN: %i x %i x %i voxels of terrain do not fit a %i x %i x %i map",
                 needed.x, needed.y, needed.z, size.x, size.y, map.get_height());
    }
    return all_fit;
}
