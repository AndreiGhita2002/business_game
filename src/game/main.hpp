//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include <vector>
#include "voxel/VoxelMap.hpp"

namespace global {
    inline raylib::Camera camera;

    // inline SingleChunkGrid* single_chunk_grid;
    inline float render_distance = 128.0f;
    inline bool limit_render_distance = false;

    inline std::vector<VoxelGrid*> voxel_grids;
    inline VoxelMap* game_map;

    // Main Function, only called inside main
    static void init();
    static void mainLoop();
    static void shutdown();

    // Update Functions, called every tick
    static void updateCamera();
    static void updateVoxelMesh();

    // Helper Functions
    bool isInRenderDistance(Vector3 v);
}

Vector3 apply_transform(Vector3 v, const Transform &t);

#endif //BUSINESS_GAME_MAIN_HPP