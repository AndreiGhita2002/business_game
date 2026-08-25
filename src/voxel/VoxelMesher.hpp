//
// Created by Andrei Ghita on 08.09.2025.
//

#ifndef BUSINESS_GAME_VOXELMESHER_HPP
#define BUSINESS_GAME_VOXELMESHER_HPP
#include "VoxelMap.hpp"

struct MaterialMesh {
    VoxelID id;
    Mesh mesh;
};

std::vector<MaterialMesh> build_chunk_mesh(const VoxelChunk& chunk, Vector3 origin, float voxelSize);

Model build_chunk_model(const std::vector<MaterialMesh>& mats, const std::map<VoxelID, Color>& voxelColourMap);

/**
 * Frees a model built by build_chunk_model() and leaves it empty.
 *
 * Always use this instead of UnloadModel(): raylib unloads the shader held by
 * every material it frees, and build_chunk_model() puts the shared voxel shader
 * on all of them, so a plain UnloadModel() takes the shader down with it and
 * every other voxel model stops drawing.
 */
void unload_chunk_model(Model& model);

#endif //BUSINESS_GAME_VOXELMESHER_HPP
