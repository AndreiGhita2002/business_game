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

class VoxelVolume;

#define VOXEL_MAP_STR "VoxelMap"

// The most voxels a map read from a file may be along any axis
constexpr int MAX_MAP_SIZE = 4096;

class VoxelMap final : public VoxelGrid {

public:
    // Keyed by chunk coordinate: chunk (cx, cy, cz) holds the voxels from
    // CHUNK_SIZE * (cx, cy, cz) onwards
    std::map<Int3, VoxelChunk> chunks;
    std::map<Int3, bool> chunk_was_updated;
    // The same question for the shadow volume, which is a separate flag because
    // the mesh and the volume are brought up to date by different calls and
    // each clears its own.
    std::map<Int3, bool> chunk_volume_dirty;
    std::map<Int3, ModelInfo> chunk_models;

    /**
     * A map of air. The terrain is the simulation's now: the game draws it in
     * with build_terrain_voxels() (entity/TerrainVoxels.hpp), and a map read
     * from a file is filled in by load_body().
     *
     * @param size_z: how tall it is, in voxels. Any number of chunks tall;
     *        one chunk unless asked otherwise.
     */
    VoxelMap(VoxelView* view, uint32_t size_x, uint32_t size_y, uint32_t size_z = CHUNK_SIZE);
    ~VoxelMap() override;

    std::string& get_grid_type() override;
    VoxelID* get_voxel(Int3 pos) override;
    Int2 get_size() override;
    void update_models() override;
    std::vector<ModelInfo*> get_models() override;
    bool in_bounds(Int3 grid_pos) const override;
    bool model_to_grid(const ModelInfo* model, Vector3 local_pos, Int3* out) override;

    /**
     * The map's body in a saved file:
     *   i32 size_x, i32 size_y, i32 size_z
     *   u32 chunk_count, then that many chunks of
     *     i32 chunk_x, i32 chunk_y, i32 chunk_z, followed by a voxel_file chunk
     * Chunks that are entirely air are written like any other, as the run
     * length encoding already flattens them to a handful of bytes.
     */
    bool write_body(std::ostream& out) override;

    /**
     * Builds a VoxelMap from the body written by write_body(). Refuses a size
     * past MAX_MAP_SIZE on any axis, and a chunk outside the map.
     */
    static VoxelGrid* load_body(std::istream& in, const voxel_file::LoadContext& ctx);

    /**
     * Writes every chunk that has changed since the last call into the volume
     * the lighting shader traces its shadow rays through.
     *
     * The chunks go in at the same place they are meshed at, so what casts a
     * shadow and what is drawn cannot drift apart.
     *
     * Says whether anything was uploaded, which is when solid_top() may have
     * moved.
     */
    bool update_volume(VoxelVolume& volume);

    /**
     * One above the highest layer with a solid voxel in it, 0 for a map of
     * air. Nothing above it can block a shadow ray, so the shader is told the
     * volume stops there (VoxelView::bindWorldVolume()).
     */
    int solid_top() const;

    /** How tall the map is, in voxels. get_size() is the other two axes. */
    int get_height() const;

    Int3 get_chunk_count() const;
    /** The chunk holding the voxel at `pos`, a grid position, or null outside the map. */
    VoxelChunk* get_chunk(Int3 pos);

    static VoxelID* get_chunk_voxel(VoxelChunk& chunk, Int3 pos);

protected:
    bool write_voxel(Int3 grid_pos, VoxelID id) override;

private:
    Int2 size;
    int height = 0;
    Int3 chunk_count;
};


#endif //BUSINESS_GAME_GAMEMAP_HPP