//
// Created by Andrei Ghita on 08.09.2025.
//

#ifndef BUSINESS_GAME_VOXELMESHER_HPP
#define BUSINESS_GAME_VOXELMESHER_HPP
#include "VoxelMap.hpp"

Mesh build_chunk_mesh(const VoxelChunk& chunk, Vector3 origin, float voxelSize);

#endif //BUSINESS_GAME_VOXELMESHER_HPP
