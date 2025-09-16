//
// Created by Andrei Ghita on 16.09.2025.
//

#ifndef BUSINESS_GAME_SINGLECHUNKGRID_HPP
#define BUSINESS_GAME_SINGLECHUNKGRID_HPP
#include "VoxelGrid.hpp"

class SingleChunkGrid final : VoxelGrid {
public:
    VoxelChunk data;
    bool was_updated;

    explicit SingleChunkGrid(const VoxelColourMap &voxel_colours);

    Int2 get_size() override;
    VoxelID *get_voxel(Int3 grid_pos) override;
    void update_models(Vector3 camera_pos) override;
    std::vector<ModelInfo*> get_models() override;
private:
    Int2 size;
};


#endif //BUSINESS_GAME_SINGLECHUNKGRID_HPP