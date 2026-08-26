//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_GAMEMAP_HPP
#define BUSINESS_GAME_GAMEMAP_HPP
#include <map>

// #include "voxel/VoxelView.hpp"
// class VoxelView;
#include "voxel/VoxelFile.hpp"
#include "voxel/VoxelGrid.hpp"

#define VOXEL_MAP_STR "VoxelMap"

class VoxelMap final : public VoxelGrid {

public:
    std::map<Int2, VoxelChunk> chunks;
    std::map<Int2, bool> chunk_was_updated;
    std::map<Int2, ModelInfo> chunk_models;

    /**
     * @param generate_terrain: when false the chunks are left as air, for a map
     *        that is about to be filled in from a file.
     */
    VoxelMap(VoxelView* view, uint32_t size_x, uint32_t size_y, bool generate_terrain = true);
    ~VoxelMap() override;

    std::string& get_grid_type() override;
    VoxelID* get_voxel(Int3 pos) override;
    Int2 get_size() override;
    void update_models() override;
    std::vector<ModelInfo*> get_models() override;
    bool set_voxel(Int3 grid_pos, VoxelID id) override;
    bool model_to_grid(const ModelInfo* model, Vector3 local_pos, Int3* out) override;

    /**
     * The map's body in a saved file:
     *   i32 size_x, i32 size_y
     *   u32 chunk_count, then that many chunks of
     *     i32 chunk_x, i32 chunk_y, followed by a voxel_file chunk
     * Chunks that are entirely air are written like any other, as the run
     * length encoding already flattens them to a handful of bytes.
     */
    bool write_body(std::ostream& out) override;

    /** Builds a VoxelMap from the body written by write_body(). */
    static VoxelGrid* load_body(std::istream& in, const voxel_file::LoadContext& ctx);

    Int2 get_chunk_count() const;
    VoxelChunk* get_chunk(Int2 pos);

    static VoxelID* get_chunk_voxel(VoxelChunk& chunk, Int3 pos);

private:
    Int2 size;
    Int2 chunk_count;
};


#endif //BUSINESS_GAME_GAMEMAP_HPP