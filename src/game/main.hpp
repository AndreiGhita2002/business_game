//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include "VoxelMap.hpp"

namespace global {
    inline raylib::Camera camera;
    inline VoxelMap* game_map;
    inline std::map<Int2, Model> chunk_models;

    static void init();
    static void mainLoop();
    static void shutdown();

    static void updateCamera();
    static void updateVoxelMesh();
}

Vector3 apply_transform(Vector3 v, Transform t);

#endif //BUSINESS_GAME_MAIN_HPP