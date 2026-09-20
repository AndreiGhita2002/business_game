//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_PICKING_HPP
#define BUSINESS_GAME_PICKING_HPP
#include <raylib.h>
#include <vector>

#include "voxel/VoxelGrid.hpp"

/**
 * Turning a ray into the voxel it landed on, and the matrix that has to agree
 * with what was drawn for that to work.
 *
 * Split out of main.hpp so that the voxel code can be linked without main(),
 * which is what lets a test binary have it.
 */

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
 * The matrix a voxel model is drawn with: the model's place inside its grid,
 * then the grid's place in the world (VoxelGrid::get_world_transform, so every
 * parent grid is folded in).
 * Both the renderer and the ray casts go through this, so that what is on the
 * screen and what a click hits can never drift apart.
 */
Matrix voxel_model_matrix(const VoxelGrid* grid, const ModelInfo& model_info);

/**
 * Walks a grid's voxels from `origin` along `dir`, both in that grid's own
 * coordinates, and answers whether a solid voxel blocks the way before the ray
 * leaves the grid.
 *
 * This is the same walk the lighting shader does for a shadow ray, kept here in
 * C++ because a shader cannot be unit tested: the awkward cases (a ray along an
 * axis, a ray starting exactly on a face, a ray leaving the world) are the same
 * in both. The two have to be changed together - see volume_blocked() in
 * resources/shaders/lighting.fs.
 *
 * A ray that starts outside the grid is not carried to it first, which is the
 * one thing the shader does differently, as it has a volume to aim at.
 *
 * @param max_steps: how many voxels to cross before giving up and answering
 *        false, the same cap the shader has.
 */
bool voxel_ray_blocked(VoxelGrid* grid, Vector3 origin, Vector3 dir, int max_steps = 256);

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

#endif //BUSINESS_GAME_PICKING_HPP
