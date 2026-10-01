//
// Created by Andrei Ghita on 29.08.2026.
//

#include "game/Picking.hpp"

#include <cfloat>
#include <cmath>
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

BoundingBox voxel_box_bounds(const Matrix matrix, const float size) {
    BoundingBox box{};
    bool first = true;

    for (int i = 0; i < 8; ++i) {
        // The eight corners of the cube, one bit of i per axis
        const Vector3 corner = Vector3Transform(Vector3{
            (i & 1) ? size : 0.0f,
            (i & 2) ? size : 0.0f,
            (i & 4) ? size : 0.0f,
        }, matrix);

        if (first) {
            box.min = corner;
            box.max = corner;
            first = false;
        } else {
            box.min = Vector3Min(box.min, corner);
            box.max = Vector3Max(box.max, corner);
        }
    }
    return box;
}

bool box_casts_onto(const BoundingBox& caster, const BoundingBox& receiver,
                    const Vector3 direction, const float reach) {
    // The box the caster sweeps through as its shadow is carried along the
    // light: where it stands, where its shadow could end, and everything in
    // between
    const Vector3 offset = Vector3Scale(direction, reach);
    const BoundingBox swept{
        Vector3Min(caster.min, Vector3Add(caster.min, offset)),
        Vector3Max(caster.max, Vector3Add(caster.max, offset)),
    };

    return swept.min.x <= receiver.max.x && swept.max.x >= receiver.min.x
        && swept.min.y <= receiver.max.y && swept.max.y >= receiver.min.y
        && swept.min.z <= receiver.max.z && swept.max.z >= receiver.min.z;
}

bool voxel_ray_blocked(VoxelGrid* grid, const Vector3 origin, const Vector3 dir, const int max_steps) {
    if (grid == nullptr) return false;

    // A component of exactly zero would divide by zero below. Nudged to
    // something tiny of its own sign, the boundary on that axis lands so far
    // off that the walk never crosses it, which is what a ray running parallel
    // to an axis should do.
    constexpr float eps_dir = 1e-6f;
    const Vector3 d = {
        std::fabs(dir.x) < eps_dir ? eps_dir : dir.x,
        std::fabs(dir.y) < eps_dir ? eps_dir : dir.y,
        std::fabs(dir.z) < eps_dir ? eps_dir : dir.z,
    };

    Int3 voxel = {
        static_cast<int>(std::floor(origin.x)),
        static_cast<int>(std::floor(origin.y)),
        static_cast<int>(std::floor(origin.z)),
    };
    const Int3 step = {
        d.x > 0.0f ? 1 : -1,
        d.y > 0.0f ? 1 : -1,
        d.z > 0.0f ? 1 : -1,
    };

    // How far along the ray one whole voxel is on each axis, and how far is
    // left to the first boundary from where the ray starts
    const Vector3 t_delta = {
        std::fabs(1.0f / d.x),
        std::fabs(1.0f / d.y),
        std::fabs(1.0f / d.z),
    };
    Vector3 t_max = {
        (static_cast<float>(voxel.x) + (step.x > 0 ? 1.0f : 0.0f) - origin.x) / d.x,
        (static_cast<float>(voxel.y) + (step.y > 0 ? 1.0f : 0.0f) - origin.y) / d.y,
        (static_cast<float>(voxel.z) + (step.z > 0 ? 1.0f : 0.0f) - origin.z) / d.z,
    };

    for (int i = 0; i < max_steps; ++i) {
        // Out of the grid: nothing left that could block the ray
        if (!grid->in_bounds(voxel)) return false;
        if (grid->is_solid(voxel)) return true;

        // Step across whichever of the three boundaries is nearest
        if (t_max.x < t_max.y) {
            if (t_max.x < t_max.z) { voxel.x += step.x; t_max.x += t_delta.x; }
            else                   { voxel.z += step.z; t_max.z += t_delta.z; }
        } else {
            if (t_max.y < t_max.z) { voxel.y += step.y; t_max.y += t_delta.y; }
            else                   { voxel.z += step.z; t_max.z += t_delta.z; }
        }
    }
    return false;
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
