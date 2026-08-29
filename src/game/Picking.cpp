//
// Created by Andrei Ghita on 29.08.2026.
//

#include "game/Picking.hpp"

#include <cfloat>
#include <raymath.h>

#include "game/Transform.hpp"

Matrix voxel_model_matrix(const VoxelGrid* grid, const ModelInfo& model_info) {
    // Two steps, in the order a scene graph applies them: the model sits
    // somewhere inside its grid, and the grid sits somewhere in the world,
    // which is its own transform with every parent's folded in.
    const Transform world = transform_transform(model_info.transform, grid->get_world_transform());

    // transform_to_matrix uses the same order as DrawModelEx - scale, rotate,
    // translate - and the model's own matrix goes on top of it
    return MatrixMultiply(model_info.model.transform, transform_to_matrix(world));
}

bool find_voxel_on_ray(const Ray ray, const std::vector<VoxelGrid*>* voxel_grids,
                       const char* grid_type, VoxelRayHit* out) {
    VoxelRayHit best{};
    best.collision.distance = FLT_MAX;
    bool found = false;

    for (VoxelGrid* grid : *voxel_grids) {
        // Filter by grid type if specified
        if (grid_type != nullptr && grid->get_grid_type().compare(grid_type) != 0)
            continue;

        for (ModelInfo* model_info : grid->get_models()) {
            // SingleChunkGrid reports a null model until it has been meshed
            if (model_info == nullptr || !model_info->do_render) continue;

            const Matrix world_mat = voxel_model_matrix(grid, *model_info);

            for (int i = 0; i < model_info->model.meshCount; ++i) {
                RayCollision c = GetRayCollisionMesh(ray, model_info->model.meshes[i], world_mat);

                if (c.hit && c.distance < best.collision.distance) {
                    best = VoxelRayHit{grid, model_info, c, world_mat};
                    found = true;
                }
            }
        }
    }

    if (found && out != nullptr) *out = best;
    return found;
}

/* Finds the closest VoxelGrid intersected by a ray.
 *  Parameters:
 *   ray         - The ray to test against (in world space)
 *   voxel_grids - Collection of grids to test
 *   grid_type   - Filter by grid type name (e.g. "SingleChunkGrid"), or nullptr to test all grids
 * Returns the closest intersected grid, or nullptr if no intersection found.
 */
VoxelGrid* find_grid_on_ray(Ray ray, const std::vector<VoxelGrid*>* voxel_grids, const char* grid_type) {
    VoxelRayHit hit{};
    if (!find_voxel_on_ray(ray, voxel_grids, grid_type, &hit)) return nullptr;
    return hit.grid;
}
