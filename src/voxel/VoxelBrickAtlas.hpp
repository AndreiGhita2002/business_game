//
// Created by Claude on 20.09.2026.
//

#ifndef BUSINESS_GAME_VOXELBRICKATLAS_HPP
#define BUSINESS_GAME_VOXELBRICKATLAS_HPP
#include <vector>

#include "voxel/VoxelVolume.hpp"

// How many bricks the atlas holds on each axis. Each brick is one chunk, so
// 8 by 8 by 4 of them is 256 grids in a 128 by 128 by 64 texture, which is a
// megabyte. Texture units are the scarce thing here, not memory: one atlas is
// one unit however many grids are in it.
#define ATLAS_BRICKS_X 8
#define ATLAS_BRICKS_Y 8
#define ATLAS_BRICKS_Z 4

/**
 * One 3D texture holding a brick of voxels per grid, so that a vehicle can
 * cast a shadow and shadow itself.
 *
 * A grid keeps its slot for as long as it lives and hands it back when it is
 * destroyed (see `VoxelGrid::volume_slot`). Its voxels are uploaded again
 * whenever they change, which is far cheaper than it sounds: a brick is 4KB.
 *
 * The grid's own transform is not baked in anywhere. The shader is handed the
 * matrix that undoes it and traces the ray in the grid's own space, where the
 * voxels are axis aligned again - which is what makes a turning wheel exact
 * rather than approximated.
 */
class VoxelBrickAtlas {
public:
    static constexpr int NO_SLOT = -1;

    /** Allocates the texture, all air. Needs an OpenGL context. */
    void create();
    void destroy();
    bool is_created() const { return volume.is_created(); }

    /** Takes a free slot, or NO_SLOT when the atlas is full. */
    int acquire_slot();

    /** Gives one back. Does not clear the voxels: the next owner overwrites. */
    void release_slot(int slot);

    /** Writes a grid's voxels into its slot. */
    void upload(int slot, const VoxelChunk& chunk) const;

    /** Where a slot's brick starts in the atlas, in voxels. */
    Int3 slot_origin(int slot) const;

    void bind(const int texture_unit) const { volume.bind(texture_unit); }
    Int3 get_size() const { return volume.get_size(); }

private:
    VoxelVolume volume;
    // One flag per slot, indexed the same way slot_origin() reads a slot
    std::vector<bool> slot_taken;
};

#endif //BUSINESS_GAME_VOXELBRICKATLAS_HPP
