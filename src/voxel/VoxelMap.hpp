//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_GAMEMAP_HPP
#define BUSINESS_GAME_GAMEMAP_HPP
#include <map>
#include "voxel/VoxelGrid.hpp"

class VoxelMap final : public VoxelGrid {

public:
    std::map<Int2, VoxelChunk> chunks;
    std::map<Int2, bool> chunk_was_updated;
    std::map<Int2, ModelInfo> chunk_models;

    VoxelMap(uint32_t size_x, uint32_t size_y);
    ~VoxelMap() override;

    VoxelID* get_voxel(Int3 pos) override;
    Int2 get_size() override;
    void update_models(Vector3 camera_pos) override;
    std::vector<ModelInfo*> get_models() override;

    Int2 get_chunk_count() const;
    VoxelChunk* get_chunk(Int2 pos);

    static VoxelID* get_chunk_voxel(VoxelChunk& chunk, Int3 pos);

private:
    Int2 size;
    Int2 chunk_count;
};


#endif //BUSINESS_GAME_GAMEMAP_HPP