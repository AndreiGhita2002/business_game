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

bool volume_march(const VolumeSolid& solid, const Int3 size, const Vector3 origin, const Vector3 dir,
                  const int max_steps, int* steps) {
    if (size.x <= 0 || size.y <= 0 || size.z <= 0) return false;

    // A component of exactly zero would divide by zero below. Nudged to
    // something tiny, the boundary on that axis lands so far away that the
    // walk never crosses it, which is what a ray parallel to an axis should do.
    constexpr float EPS_DIR = 1e-6f;
    const Vector3 d = {
        std::fabs(dir.x) < EPS_DIR ? EPS_DIR : dir.x,
        std::fabs(dir.y) < EPS_DIR ? EPS_DIR : dir.y,
        std::fabs(dir.z) < EPS_DIR ? EPS_DIR : dir.z,
    };
    const Vector3 inv_d = {1.0f / d.x, 1.0f / d.y, 1.0f / d.z};

    // Skip the empty space in front of the volume: the ray against the box,
    // near and far
    const Vector3 t_lo = {(0.0f - origin.x) * inv_d.x, (0.0f - origin.y) * inv_d.y, (0.0f - origin.z) * inv_d.z};
    const Vector3 t_hi = {(static_cast<float>(size.x) - origin.x) * inv_d.x,
                          (static_cast<float>(size.y) - origin.y) * inv_d.y,
                          (static_cast<float>(size.z) - origin.z) * inv_d.z};
    const Vector3 t_near = Vector3Min(t_lo, t_hi);
    const Vector3 t_far = Vector3Max(t_lo, t_hi);
    const float t_enter = std::max({t_near.x, t_near.y, t_near.z});
    const float t_exit = std::min({t_far.x, t_far.y, t_far.z});

    // The ray never crosses the volume at all, or only behind its start
    if (t_exit < std::max(t_enter, 0.0f)) return false;

    // Start where the ray stands, or just inside the box when it is outside.
    // The nudge keeps the first voxel off the boundary itself.
    const float t_start = std::max(t_enter, 0.0f) + 1e-4f;
    const Vector3 p = Vector3Add(origin, Vector3Scale(d, t_start));

    Int3 voxel = {
        std::clamp(static_cast<int>(std::floor(p.x)), 0, size.x - 1),
        std::clamp(static_cast<int>(std::floor(p.y)), 0, size.y - 1),
        std::clamp(static_cast<int>(std::floor(p.z)), 0, size.z - 1),
    };
    const Int3 step = {d.x > 0.0f ? 1 : -1, d.y > 0.0f ? 1 : -1, d.z > 0.0f ? 1 : -1};
    const Vector3 t_delta = {std::fabs(inv_d.x), std::fabs(inv_d.y), std::fabs(inv_d.z)};
    Vector3 t_max = {
        (static_cast<float>(voxel.x) + (step.x > 0 ? 1.0f : 0.0f) - p.x) * inv_d.x,
        (static_cast<float>(voxel.y) + (step.y > 0 ? 1.0f : 0.0f) - p.y) * inv_d.y,
        (static_cast<float>(voxel.z) + (step.z > 0 ? 1.0f : 0.0f) - p.z) * inv_d.z,
    };

    for (int i = 0; i < max_steps; ++i) {
        // Out of this volume: nothing left in it that could block the ray
        if (voxel.x < 0 || voxel.y < 0 || voxel.z < 0 || voxel.x >= size.x || voxel.y >= size.y ||
            voxel.z >= size.z)
            return false;

        if (steps != nullptr) *steps += 1;
        if (solid(voxel)) return true;

        // Step across the nearest boundary of the three
        if (t_max.x < t_max.y) {
            if (t_max.x < t_max.z) { voxel.x += step.x; t_max.x += t_delta.x; }
            else                   { voxel.z += step.z; t_max.z += t_delta.z; }
        } else {
            if (t_max.y < t_max.z) { voxel.y += step.y; t_max.y += t_delta.y; }
            else                   { voxel.z += step.z; t_max.z += t_delta.z; }
        }
    }
    // Ran out of steps, which is called lit
    return false;
}

bool volume_march_coarse(const VolumeSolid& solid, const VolumeSolid& coarse_solid, const Int3 size,
                         const Vector3 origin, const Vector3 dir, const int max_steps, int* steps) {
    int local_steps = 0;
    int& count = steps != nullptr ? *steps : local_steps;

    // The coarse volume covers the fine one, its last cell cut short where the
    // fine one does not fill it
    const Int3 cells = {
        (size.x + WORLD_COARSE - 1) / WORLD_COARSE,
        (size.y + WORLD_COARSE - 1) / WORLD_COARSE,
        (size.z + WORLD_COARSE - 1) / WORLD_COARSE,
    };
    // The same ray in cell coordinates: a cell is WORLD_COARSE voxels, and the
    // direction can stay as it is, as only the order the boundaries are
    // crossed in matters
    const Vector3 cell_origin = Vector3Scale(origin, 1.0f / WORLD_COARSE);

    // The coarse walk is volume_march() over the cells, with a cell that is
    // occupied walked again voxel by voxel, boxed to that cell
    const VolumeSolid cell_blocks = [&](const Int3 cell) {
        if (!coarse_solid(cell)) return false;
        const Int3 lo = {cell.x * WORLD_COARSE, cell.y * WORLD_COARSE, cell.z * WORLD_COARSE};
        const Int3 hi = {
            std::min(lo.x + WORLD_COARSE, size.x),
            std::min(lo.y + WORLD_COARSE, size.y),
            std::min(lo.z + WORLD_COARSE, size.z),
        };
        const VolumeSolid in_cell = [&](const Int3 v) { return solid(Int3{lo.x + v.x, lo.y + v.y, lo.z + v.z}); };
        const Vector3 cell_relative = {origin.x - static_cast<float>(lo.x), origin.y - static_cast<float>(lo.y),
                                       origin.z - static_cast<float>(lo.z)};
        return volume_march(in_cell, Int3{hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}, cell_relative, dir,
                            max_steps, &count);
    };

    // Each cell costs a step as well as whatever its voxels cost, and the cap
    // is on the two together: once it is spent nothing more can block the
    // ray, so it comes out lit (the shader stops there and then)
    const VolumeSolid capped = [&](const Int3 cell) {
        if (count >= max_steps) return false;
        return cell_blocks(cell);
    };
    return volume_march(capped, cells, cell_origin, dir, max_steps, &count);
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

            // Every model is meshed inside its own chunk's cube, so a ray that
            // misses the cube, or reaches it only past the best hit so far,
            // cannot hit a triangle in it. raylib's mesh test has no such
            // early out and walks every triangle, which over a whole island
            // is most of a frame.
            const RayCollision box = GetRayCollisionBox(ray, voxel_box_bounds(world_mat, CHUNK_SIZE));
            if (!box.hit || box.distance >= best.collision.distance) continue;

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
