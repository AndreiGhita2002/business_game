//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_GAMEMAP_HPP
#define BUSINESS_GAME_GAMEMAP_HPP
#include <map>

// #include "voxel/VoxelView.hpp"
// class VoxelView;
#include "voxel/VoxelGrid.hpp"

class VoxelMap final : public VoxelGrid {

public:
    std::map<Int2, VoxelChunk> chunks;
    std::map<Int2, bool> chunk_was_updated;
    std::map<Int2, ModelInfo> chunk_models;

    VoxelMap(VoxelView* view, uint32_t size_x, uint32_t size_y);
    ~VoxelMap() override;

    std::string& get_grid_type() override;
    VoxelID* get_voxel(Int3 pos) override;
    Int2 get_size() override;
    void update_models() override;
    std::vector<ModelInfo*> get_models() override;
    void set_transform(Transform new_transform) override;
    bool set_voxel(Int3 grid_pos, VoxelID id) override;
    bool model_to_grid(const ModelInfo* model, Vector3 local_pos, Int3* out) override;

    Int2 get_chunk_count() const;
    VoxelChunk* get_chunk(Int2 pos);

    static VoxelID* get_chunk_voxel(VoxelChunk& chunk, Int3 pos);

private:
    Int2 size;
    Int2 chunk_count;
};


#endif //BUSINESS_GAME_GAMEMAP_HPP