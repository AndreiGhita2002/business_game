//
// Created by Andrei Ghita on 16.09.2025.
//

#ifndef BUSINESS_GAME_SINGLECHUNKGRID_HPP
#define BUSINESS_GAME_SINGLECHUNKGRID_HPP
#include "VoxelFile.hpp"
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
    Transform get_transform() const override;

    /**
     * The grid's body in a saved file: one voxel_file chunk, nothing else. The
     * size is always CHUNK_SIZE, and the file header already records it.
     */
    bool write_body(std::ostream& out) override;

    /** Builds a SingleChunkGrid from the body written by write_body(). */
    static VoxelGrid* load_body(std::istream& in, const voxel_file::LoadContext& ctx);
private:
    Int2 size;
    std::optional<ModelInfo> model;
};


#endif //BUSINESS_GAME_SINGLECHUNKGRID_HPP