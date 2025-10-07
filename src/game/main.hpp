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
#include "VoxelEditor.hpp"
#include "VoxelView.hpp"
#include "voxel/VoxelMap.hpp"

namespace global {
    inline float render_distance = 128.0f;
    inline bool limit_render_distance = false;

    inline raylib::Shader voxel_shader;
    inline float ambient[4] = {0.06f, 0.06f, 0.06f, 1.0f};

    inline std::unique_ptr<ViewNode> root_view;
    inline VoxelView* voxel_view;

    inline VoxelEditor voxel_editor; //todo convert to view node

    // Main Functions, only called inside main
    static void init();
    static void mainLoop();
    static void shutdown();

    std::string loadFile(const std::string& path);
    raylib::Shader loadAndPatchShader(const std::string& shader_path, int light_count);
}

Vector3 apply_transform(Vector3 v, const Transform &t);

Transform transform_transform(const Transform& base, const Transform& applied);

#endif //BUSINESS_GAME_MAIN_HPP