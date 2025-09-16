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

#endif //BUSINESS_GAME_VOXELMESHER_HPP
