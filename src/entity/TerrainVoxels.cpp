//
// Created by Claude on 09.10.2026.
//

#include "entity/TerrainVoxels.hpp"

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
        case sim::BlockType::Air: return 0;
        case sim::BlockType::Stone: return STONE_VOXEL;
        case sim::BlockType::Dirt: return DIRT_VOXEL;
        case sim::BlockType::Grass: {
            // Green on the top voxel of each column, which is one lower under
            // a lowered edge
            const bool top = in_block.z == LAST ||
                (in_block.z == LAST - 1 && lowered_away(detail, Int3{in_block.x, in_block.y, LAST}));
            return top ? GRASS_VOXEL : DIRT_VOXEL;
        }
    }
    return 0;
}

Int3 terrain_voxel_size(const sim::Terrain& terrain) {
    return Int3{terrain.size_x() * BLOCK_VOXELS, terrain.size_y() * BLOCK_VOXELS,
                terrain.size_z() * BLOCK_VOXELS};
}

bool build_terrain_voxels(VoxelMap& map, const sim::Terrain& terrain) {
    // Everything goes, so a terrain with fewer blocks than the last one (a
    // loaded game) leaves no stale ground behind
    for (auto& [chunk_pos, chunk] : map.chunks) {
        chunk.fill(0);
        map.chunk_was_updated[chunk_pos] = true;
        map.chunk_volume_dirty[chunk_pos] = true;
    }

    // Written straight into the chunks rather than through set_voxel(), which
    // would mark the neighbouring chunks of every voxel for remeshing one by
    // one. Every chunk is already marked above.
    bool all_fit = true;
    for (int32_t bz = 0; bz < terrain.size_z(); ++bz) {
        for (int32_t by = 0; by < terrain.size_y(); ++by) {
            for (int32_t bx = 0; bx < terrain.size_x(); ++bx) {
                const sim::BlockType type = terrain.get(bx, by, bz);
                const BlockDetail detail = block_detail(terrain, bx, by, bz);
                // Air is only drawn for its trims
                if (type == sim::BlockType::Air && detail.trims == 0) continue;

                for (int vz = 0; vz < BLOCK_VOXELS; ++vz) {
                    for (int vy = 0; vy < BLOCK_VOXELS; ++vy) {
                        for (int vx = 0; vx < BLOCK_VOXELS; ++vx) {
                            const Int3 grid_pos{
                                bx * BLOCK_VOXELS + vx,
                                by * BLOCK_VOXELS + vy,
                                bz * BLOCK_VOXELS + vz,
                            };
                            // in_bounds first: get_voxel wraps an out of
                            // range coordinate into a chunk rather than
                            // refusing it
                            if (!map.in_bounds(grid_pos)) {
                                all_fit = false;
                                continue;
                            }
                            *map.get_voxel(grid_pos) = block_voxel(type, detail, Int3{vx, vy, vz});
                        }
                    }
                }
            }
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
