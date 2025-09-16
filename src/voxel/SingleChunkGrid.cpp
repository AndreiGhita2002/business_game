//
// Created by Andrei Ghita on 16.09.2025.
//

#include "SingleChunkGrid.hpp"

SingleChunkGrid::SingleChunkGrid(const VoxelColourMap &voxel_colours) {
    this->voxel_colours = voxel_colours;
    transform = identity();
    size = Int2(CHUNK_SIZE, CHUNK_SIZE);
    was_updated = false;
    data = VoxelChunk();
}

Int2 SingleChunkGrid::get_size() {
    return Int2(size.x, size.y);
}

VoxelID* SingleChunkGrid::get_voxel(Int3 grid_pos) {
    return &data[grid_pos.x
        + grid_pos.y * CHUNK_SIZE
        + grid_pos.z * CHUNK_SIZE * CHUNK_SIZE];
}

void SingleChunkGrid::update_models(Vector3 camera_pos) {
    //todo
}

std::vector<ModelInfo *> SingleChunkGrid::get_models() {
    //todo
    return std::vector<ModelInfo *>();
}
