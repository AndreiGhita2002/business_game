//
// Created by Andrei Ghita on 16.09.2025.
//

#ifndef BUSINESS_GAME_SINGLECHUNKGRID_HPP
#define BUSINESS_GAME_SINGLECHUNKGRID_HPP
#include "VoxelGrid.hpp"

class SingleChunkGrid final : public VoxelGrid {
public:
    Transform transform;
    VoxelChunk data;
    bool was_updated;

    explicit SingleChunkGrid(VoxelView* view, const VoxelColourMap &voxel_colours);

    std::string& get_grid_type() override;
    Int2 get_size() override;
    VoxelID *get_voxel(Int3 grid_pos) override;
    void update_models() override;
    std::vector<ModelInfo*> get_models() override;
    void set_transform(Transform new_transform) override;
private:
    Int2 size;
    std::optional<ModelInfo> model;
};


#endif //BUSINESS_GAME_SINGLECHUNKGRID_HPP