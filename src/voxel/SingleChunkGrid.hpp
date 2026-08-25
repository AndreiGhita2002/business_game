//
// Created by Andrei Ghita on 16.09.2025.
//

#ifndef BUSINESS_GAME_SINGLECHUNKGRID_HPP
#define BUSINESS_GAME_SINGLECHUNKGRID_HPP
#include "VoxelGrid.hpp"

#define SINGLE_CHUNK_GRID_STR "SingleChunkGrid"

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
    bool set_voxel(Int3 grid_pos, VoxelID id) override;
    bool model_to_grid(const ModelInfo* model, Vector3 local_pos, Int3* out) override;
private:
    Int2 size;
    std::optional<ModelInfo> model;
};


#endif //BUSINESS_GAME_SINGLECHUNKGRID_HPP