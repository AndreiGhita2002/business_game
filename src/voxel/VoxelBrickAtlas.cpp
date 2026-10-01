//
// Created by Claude on 20.09.2026.
//

#include "voxel/VoxelBrickAtlas.hpp"

#include <raylib.h>

void VoxelBrickAtlas::create() {
    volume.create(Int3{
        ATLAS_BRICKS_X * CHUNK_SIZE,
        ATLAS_BRICKS_Y * CHUNK_SIZE,
        ATLAS_BRICKS_Z * CHUNK_SIZE
    });
    slot_taken.assign(ATLAS_BRICKS_X * ATLAS_BRICKS_Y * ATLAS_BRICKS_Z, false);
}

void VoxelBrickAtlas::destroy() {
    volume.destroy();
    slot_taken.clear();
}

int VoxelBrickAtlas::acquire_slot() {
    for (size_t i = 0; i < slot_taken.size(); ++i) {
        if (slot_taken[i]) continue;
        slot_taken[i] = true;
        return static_cast<int>(i);
    }
    // Every brick is spoken for. The grid simply casts no shadow rather than
    // taking someone else's slot, which would make two grids trade places
    // every frame.
    TraceLog(LOG_WARNING, "VOXELBRICKATLAS: all %zu slots are taken", slot_taken.size());
    return NO_SLOT;
}

void VoxelBrickAtlas::release_slot(const int slot) {
    if (slot < 0 || static_cast<size_t>(slot) >= slot_taken.size()) return;
    slot_taken[slot] = false;
}

void VoxelBrickAtlas::upload(const int slot, const VoxelChunk& chunk) const {
    if (slot < 0 || static_cast<size_t>(slot) >= slot_taken.size()) return;
    volume.upload_chunk(slot_origin(slot), chunk);
}

Int3 VoxelBrickAtlas::slot_origin(const int slot) const {
    const int bx = slot % ATLAS_BRICKS_X;
    const int by = (slot / ATLAS_BRICKS_X) % ATLAS_BRICKS_Y;
    const int bz = slot / (ATLAS_BRICKS_X * ATLAS_BRICKS_Y);
    return Int3{bx * CHUNK_SIZE, by * CHUNK_SIZE, bz * CHUNK_SIZE};
}
