//
// Created by Claude on 09.10.2026.
//

#include "entity/TerrainVoxels.hpp"

#include <raylib.h>

#include "voxel/VoxelMap.hpp"

VoxelID block_voxel(const sim::BlockType type, const Int3 in_block) {
    switch (type) {
        case sim::BlockType::Air: return 0;
        case sim::BlockType::Stone: return STONE_VOXEL;
        case sim::BlockType::Dirt: return DIRT_VOXEL;
        case sim::BlockType::Grass:
            return in_block.z == BLOCK_VOXELS - 1 ? GRASS_VOXEL : DIRT_VOXEL;
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
                if (type == sim::BlockType::Air) continue;

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
                            *map.get_voxel(grid_pos) = block_voxel(type, Int3{vx, vy, vz});
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
