//
// Created by Claude on 20.09.2026.
//

#include "voxel/VoxelVolume.hpp"

#include <cstdlib>

#include <raylib.h>
// raylib has no 3D textures, so this file calls OpenGL itself. glad comes from
// raylib's own sources and its function pointers are loaded when the window is
// created, so nothing here may run before that.
// TODO(claude): a web build needs the GLES 3.0 path here, and a GLSL ES 3.00
//  variant of resources/shaders/lighting.fs. Desktop only for now.
#include "external/glad.h"

VoxelVolume::~VoxelVolume() {
    destroy();
}

void VoxelVolume::create(const Int3 size_voxels, const VoxelID* voxels) {
    destroy();

    if (size_voxels.x <= 0 || size_voxels.y <= 0 || size_voxels.z <= 0) {
        TraceLog(LOG_WARNING, "VOXELVOLUME: asked for a %i by %i by %i volume",
                 size_voxels.x, size_voxels.y, size_voxels.z);
        return;
    }
    size = size_voxels;

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_3D, texture_id);

    // Without voxels to start from, filled with zeros rather than left
    // undefined, so that a volume starts as air everywhere, including any part
    // of it no chunk is ever written to. calloc rather than a vector: a volume
    // is tens of megabytes, which a vector fills a byte at a time in a debug
    // build, where calloc hands back pages that are already zero.
    VoxelID* air = nullptr;
    if (voxels == nullptr) {
        air = static_cast<VoxelID*>(
            std::calloc(static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * static_cast<size_t>(size.z), 1));
        voxels = air;
    }

    // One byte per voxel, so rows are not padded to any alignment
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, size.x, size.y, size.z, 0,
                 GL_RED, GL_UNSIGNED_BYTE, voxels);
    std::free(air);

    // Nearest and clamped: the shader reads whole voxels, and blending two of
    // them into a half solid one would put shadows where no voxel is.
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_3D, 0);

    TraceLog(LOG_INFO, "VOXELVOLUME: [ID %u] %i by %i by %i voxels created",
             texture_id, size.x, size.y, size.z);
}

void VoxelVolume::destroy() {
    if (texture_id != 0) {
        glDeleteTextures(1, &texture_id);
        texture_id = 0;
    }
    size = Int3{0, 0, 0};
}

void VoxelVolume::upload_chunk(const Int3 origin, const VoxelChunk& chunk) const {
    if (texture_id == 0) return;

    // Refused rather than wrapped or clipped: a volume is built to whole
    // chunks, so a chunk that does not fit is a mistake somewhere else.
    if (origin.x < 0 || origin.y < 0 || origin.z < 0 ||
        origin.x + CHUNK_SIZE > size.x ||
        origin.y + CHUNK_SIZE > size.y ||
        origin.z + CHUNK_SIZE > size.z) {
        TraceLog(LOG_WARNING, "VOXELVOLUME: [ID %u] a chunk at %i,%i,%i does not fit in %i by %i by %i",
                 texture_id, origin.x, origin.y, origin.z, size.x, size.y, size.z);
        return;
    }

    glBindTexture(GL_TEXTURE_3D, texture_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // A VoxelChunk is indexed x + y*CHUNK_SIZE + z*CHUNK_SIZE*CHUNK_SIZE, which
    // is the order glTexSubImage3D reads its input in, so the array goes over
    // as it stands.
    glTexSubImage3D(GL_TEXTURE_3D, 0,
                    origin.x, origin.y, origin.z,
                    CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE,
                    GL_RED, GL_UNSIGNED_BYTE, chunk.data());
    glBindTexture(GL_TEXTURE_3D, 0);
}

void VoxelVolume::bind(const int texture_unit) const {
    if (texture_id == 0) return;

    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_3D, texture_id);
    // Back to the unit raylib expects to be the current one, as it sets its
    // material textures up from unit 0 as it draws. The binding above stays:
    // it belongs to its own unit, not to whichever one is active.
    glActiveTexture(GL_TEXTURE0);
}
