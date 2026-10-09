//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_PICKING_HPP
#define BUSINESS_GAME_PICKING_HPP
#include <functional>
#include <raylib.h>
#include <vector>

#include "voxel/VoxelGrid.hpp"
#include "voxel/VoxelVolume.hpp"   // WORLD_COARSE

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
 * in both. The two have to be changed together - see march_volume() in
 * resources/shaders/lighting.fs.
 *
 * A ray that starts outside the grid is not carried to it first, which is the
 * one thing the shader does differently, as it has a volume to aim at.
 *
 * @param max_steps: how many voxels to cross before giving up and answering
 *        false, the same cap the shader has.
 */
bool voxel_ray_blocked(VoxelGrid* grid, Vector3 origin, Vector3 dir, int max_steps = 256);

/** Whether voxel `v` of a volume is solid, for the marches below. */
using VolumeSolid = std::function<bool(Int3 v)>;

/**
 * The shader's march_volume() as it is, line for line: a volume of `size`
 * voxels from 0, a ray from `origin` along `dir` in voxel coordinates, carried
 * to the volume's box first when it starts outside it. Counts each voxel it
 * reads into `steps` and gives up (lit) after `max_steps` of its own.
 * Change the two together.
 */
bool volume_march(const VolumeSolid& solid, Int3 size, Vector3 origin, Vector3 dir, int max_steps, int* steps);

/**
 * The shader's march_world(), the two level walk a shadow ray takes through
 * the world: across the coarse cells (`coarse_solid`, in cell coordinates),
 * and through the voxels of a cell only where the cell is occupied, with
 * volume_march() boxed to that cell. Empty space costs a step a cell rather
 * than a step a voxel, and the answer is the same as volume_march() over the
 * whole volume, which the tests hold it to. `max_steps` caps the cells and
 * voxels read together. Change the two together.
 */
bool volume_march_coarse(const VolumeSolid& solid, const VolumeSolid& coarse_solid, Int3 size,
                         Vector3 origin, Vector3 dir, int max_steps, int* steps);

/**
 * The world space box that the cube of `size` voxels a grid or model is built
 * in fills once `matrix` has been applied to it.
 *
 * Every corner goes through the matrix and the result is the box around all
 * eight, so a turned grid gives the box its corners reach rather than a box
 * that has been turned.
 */
BoundingBox voxel_box_bounds(Matrix matrix, float size);

/**
 * Whether `caster` could throw a shadow onto `receiver` with the light
 * travelling along `direction`, within `reach` world units.
 *
 * The caster's box is dragged along the direction and the two boxes are tested
 * for overlap, so the answer is "no" or "maybe": it is there to keep a volume
 * out of a draw call's shadow list, never to decide what is actually in shadow.
 */
bool box_casts_onto(const BoundingBox& caster, const BoundingBox& receiver,
                    Vector3 direction, float reach);

/** Where a ray met a grid's first solid voxel. */
struct GridRayHit {
    Int3 voxel;
    // The face it came in through, as the step out of the voxel through that
    // face: (0, 0, 1) for a voxel hit from above
    Int3 normal;
    // How far along the ray, in the ray's own units
    float t;
};

/**
 * Walks `grid`'s voxels along a ray given in the grid's own coordinates (x, y,
 * z up, a voxel a unit), from where it enters the box voxel_extent() makes, and
 * finds the first solid one no further than `max_t` along it.
 *
 * What picking is built on, in place of testing every triangle of the meshes:
 * a voxel walk visits only the voxels on the ray, and needs no CPU copy of
 * any mesh. A ray that starts inside a solid voxel hits it at once, through
 * the face it is pointing most nearly away from.
 */
bool grid_ray_cast(VoxelGrid* grid, Vector3 origin, Vector3 dir, float max_t, GridRayHit* out);

/**
 * Does a ray cast and returns the closest voxel model on the line.
 *
 * Each grid is walked voxel by voxel (grid_ray_cast()) with the ray carried
 * into its own space, so a moved, turned or scaled grid is picked exactly. The
 * hit's model is the one that draws the voxel; a voxel whose chunk is not
 * meshed yet, or is out of render distance, is not drawn and so not hit.
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
