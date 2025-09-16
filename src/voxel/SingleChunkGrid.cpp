//
// Created by Andrei Ghita on 16.09.2025.
//

#include "SingleChunkGrid.hpp"

#include "VoxelMesher.hpp"
#include "game/main.hpp"

SingleChunkGrid::SingleChunkGrid(const VoxelColourMap &voxel_colours) {
    this->voxel_colours = voxel_colours;
    transform = identity();
    size = Int2(CHUNK_SIZE, CHUNK_SIZE);
    was_updated = true;
    data = VoxelChunk();
    model = {};
}

Int2 SingleChunkGrid::get_size() {
    return Int2(size.x, size.y);
}

VoxelID* SingleChunkGrid::get_voxel(Int3 grid_pos) {
    return &data[grid_pos.x
        + grid_pos.y * CHUNK_SIZE
        + grid_pos.z * CHUNK_SIZE * CHUNK_SIZE];
}

void SingleChunkGrid::update_models() {
    if (global::isInRenderDistance(transform.translation)) {
        if (was_updated) {
            auto meshes = build_chunk_mesh(data, Vector3{0.0,0.0,0.0}, 1.0f);
            auto new_model = build_chunk_model(meshes, *voxel_colours);

            model = ModelInfo{true, new_model, transform};

            was_updated = false;
        }
    } else if (model.has_value()) {
        model->do_render = false;
    }
}

std::vector<ModelInfo *> SingleChunkGrid::get_models() {
    auto out = std::vector<ModelInfo*>();
    if (model.has_value()) {
        out.emplace_back(&model.value());
    } else {
        out.emplace_back(nullptr);
    }
    return out;
}
