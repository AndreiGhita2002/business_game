//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include <RenderTexture.hpp>
#include <Shader.hpp>
#include <vector>

#include "ViewNode.hpp"
#include "game/Picking.hpp"
#include "game/Transform.hpp"
#include "ui/VoxelEditor.hpp"
#include "voxel/VoxelView.hpp"
#include "voxel/VoxelMap.hpp"

namespace global {
    inline float render_distance = 128.0f;
    inline bool limit_render_distance = false;

    inline raylib::Shader voxel_shader;
    inline float ambient[4] = {0.06f, 0.06f, 0.06f, 1.0f};

    inline std::unique_ptr<ViewNode> root_view;

    // Main Functions, only called inside main
    static void init();
    static void mainLoop();
    static void shutdown();

    std::string loadFile(const std::string& path);
    raylib::Shader loadAndPatchShader(const std::string& shader_path, int light_count);
}

// The transform maths lives in game/Transform.hpp and the ray casts in
// game/Picking.hpp, both included above so that everything that used to reach
// them through this header still can. Include the narrow one directly in new
// code: this header drags in the window, the shaders and the whole view tree.

#endif //BUSINESS_GAME_MAIN_HPP