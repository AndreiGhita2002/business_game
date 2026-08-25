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

void apply_transform(Vector3* position, Quaternion* rotation, Vector3* scale, const Transform& t);

Vector3 apply_transform_trans(Vector3 v, const Transform &t);

Quaternion apply_transform_rot(Quaternion rot, const Transform &t);

Vector3 apply_transform_scale(Vector3 scale, const Transform &t);

/**
 * Everything the editor needs about a voxel that a ray landed on.
 */
struct VoxelRayHit {
    VoxelGrid* grid;
    ModelInfo* model;
    // Point and normal in world space
    RayCollision collision;
    // The matrix the model was drawn with, for going back into model space
    Matrix world_matrix;
};

/**
 * The matrix a voxel model is drawn with.
 * Both the renderer and the ray casts go through this, so that what is on the
 * screen and what a click hits can never drift apart.
 */
Matrix voxel_model_matrix(const VoxelGrid* grid, const ModelInfo& model_info);

/**
 * Does a ray cast and returns the closest voxel model on the line.
 *
 * @param ray: the ray, for mouse ray get it from `GetScreenToWorldRay`
 * @param voxel_grids: a collection of grids that the function should look through.
 * @param grid_type: only select grids of this type. If null, then return any grid.
 * @param out: filled in with the closest hit, untouched when nothing was hit.
 * @return whether anything was hit.
 */
bool find_voxel_on_ray(Ray ray, const std::vector<VoxelGrid*>* voxel_grids, const char* grid_type, VoxelRayHit* out);

/**
 * Does a ray cast and selects the first grid on the line.
 *
 * @param ray: the ray, for mouse ray get it from `GetScreenToWorldRay`
 * @param voxel_grids: a collection of grids that the function should look through.
 * @param grid_type: only select grids of this type. If null, then return any grid.
 * @return The first grid that was found on the ray.
 */
VoxelGrid* find_grid_on_ray(Ray ray, const std::vector<VoxelGrid*>* voxel_grids, const char* grid_type);

Transform transform_transform(const Transform& base, const Transform& applied);

Matrix transform_to_matrix(Transform t);

void print_matrix(const Matrix& mat);

#endif //BUSINESS_GAME_MAIN_HPP