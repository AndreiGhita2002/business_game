//
// Created by Claude on 20.09.2026.
//

#ifndef BUSINESS_GAME_VOXELVOLUME_HPP
#define BUSINESS_GAME_VOXELVOLUME_HPP
#include "voxel/VoxelGrid.hpp"

/**
 * A grid's voxels as a 3D texture, which is what the lighting shader walks
 * when it traces a shadow ray.
 *
 * The texture holds one byte per voxel, the VoxelID itself rather than a plain
 * solid flag: it costs the same and leaves room for a material that lets light
 * through later. It is read with texelFetch and never filtered, so a voxel is
 * either there or it is not.
 *
 * The voxels sit in the texture in grid order - x fastest, then y, then z (up)
 * - which is the order a VoxelChunk is already laid out in, so a chunk goes to
 * the GPU exactly as it is held in memory. The shader undoes the mesher's axis
 * swap on its side, see resources/shaders/lighting.fs.
 *
 * Everything here needs an OpenGL context, so nothing may be called before the
 * window is open. raylib has no 3D texture calls of its own (rlgl only ever
 * binds GL_TEXTURE_2D), which is why this talks to OpenGL directly.
 */
class VoxelVolume {
public:
    VoxelVolume() = default;
    ~VoxelVolume();

    // The texture is owned, and a copy would free it twice
    VoxelVolume(const VoxelVolume&) = delete;
    VoxelVolume& operator=(const VoxelVolume&) = delete;

    /**
     * Allocates the texture as all air. Sizes are in voxels, and should be
     * whole chunks, as upload_chunk() refuses a chunk that hangs over the edge.
     * Replaces whatever was there before.
     */
    void create(Int3 size_voxels);

    /** Frees the texture. Safe to call on a volume that has none. */
    void destroy();

    bool is_created() const { return texture_id != 0; }

    /** Writes one chunk's voxels into the volume at `origin`, in voxels. */
    void upload_chunk(Int3 origin, const VoxelChunk& chunk) const;

    /**
     * Binds the texture to a texture unit, where it stays until something else
     * binds over it. Pick a unit above the ones raylib hands to material maps.
     */
    void bind(int texture_unit) const;

    Int3 get_size() const { return size; }

private:
    unsigned int texture_id = 0;
    Int3 size{0, 0, 0};
};

#endif //BUSINESS_GAME_VOXELVOLUME_HPP
