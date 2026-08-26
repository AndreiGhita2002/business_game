//
// Created by Andrei Ghita on 16.09.2025.
//

#include "SingleChunkGrid.hpp"

#include <cmath>
#include <istream>
#include <memory>
#include <ostream>

#include "VoxelMesher.hpp"
#include "game/main.hpp"

SingleChunkGrid::SingleChunkGrid(VoxelView* view, const VoxelColourMap &voxel_colours)
    : VoxelGrid(view)
{
    this->voxel_colours = voxel_colours;
    transform = identity();
    size = Int2(CHUNK_SIZE, CHUNK_SIZE);
    was_updated = true;
    data = VoxelChunk();
    model = {};
}

std::string& SingleChunkGrid::get_grid_type() {
    static std::string TYPE = SINGLE_CHUNK_GRID_STR;
    return TYPE;
}

Int2 SingleChunkGrid::get_size() {
    return Int2(size.x, size.y);
}

VoxelID* SingleChunkGrid::get_voxel(Int3 grid_pos) {
    return &data[grid_pos.x
        + grid_pos.y * CHUNK_SIZE
        + grid_pos.z * CHUNK_SIZE * CHUNK_SIZE];
}

void SingleChunkGrid::update_models() {
    // The render distance is a question about the world, so the grid's own
    // transform is not enough: a grid hanging off a moved parent has moved too
    const Transform world = get_world_transform();

    if (view->isInRenderDistance(world.translation)) {
        if (was_updated) {
            auto meshes = build_chunk_mesh(data, Vector3{0.0,0.0,0.0}, 1.0f);
            auto new_model = build_chunk_model(meshes, *voxel_colours);

            // The previous model would leak its GPU buffers otherwise, and the
            // editor meshes this grid again on every voxel it places
            if (model.has_value()) unload_chunk_model(model->model);

            // The mesh is built at the grid's own origin, so the model sits at
            // identity inside the grid. Where the grid is in the world is added
            // by voxel_model_matrix() at draw time, which is why moving a grid
            // never needs a remesh.
            model = ModelInfo{true, new_model, identity()};

            was_updated = false;
        }
    } else if (model.has_value()) {
        model->do_render = false;
    }
}

bool SingleChunkGrid::set_voxel(const Int3 grid_pos, const VoxelID id) {
    // get_voxel() does no bounds checking, so it is done here
    if (grid_pos.x < 0 || grid_pos.x >= CHUNK_SIZE ||
        grid_pos.y < 0 || grid_pos.y >= CHUNK_SIZE ||
        grid_pos.z < 0 || grid_pos.z >= CHUNK_SIZE)
        return false;

    *get_voxel(grid_pos) = id;
    was_updated = true;
    return true;
}

bool SingleChunkGrid::model_to_grid(const ModelInfo* model, const Vector3 local_pos, Int3* out) {
    if (!model || !this->model.has_value() || model != &this->model.value()) return false;

    // The mesh is built at the origin with a voxel size of 1, so the local
    // position is already in voxels. Model space is (x, z, y) in grid terms.
    *out = Int3{
        static_cast<int>(floorf(local_pos.x)),
        static_cast<int>(floorf(local_pos.z)),
        static_cast<int>(floorf(local_pos.y)),
    };
    return true;
}

bool SingleChunkGrid::write_body(std::ostream& out) {
    return voxel_file::write_chunk(out, data);
}

VoxelGrid* SingleChunkGrid::load_body(std::istream& in, const voxel_file::LoadContext& ctx) {
    // Without a palette from the caller or the file the grid still needs a map
    // to look colours up in, even if nothing in it can be drawn
    const VoxelColourMap palette = ctx.palette
        ? ctx.palette
        : std::make_shared<std::map<VoxelID, Color>>();

    auto* grid = new SingleChunkGrid(ctx.view, palette);
    if (!voxel_file::read_chunk(in, &grid->data)) {
        delete grid;
        return nullptr;
    }
    grid->was_updated = true;
    return grid;
}

std::vector<ModelInfo *> SingleChunkGrid::get_models() {
    auto out = std::vector<ModelInfo*>();
    if (model.has_value()) {
        out.emplace_back(&model.value());
    } else {
        out.emplace_back(nullptr);
    }
    return out;
}
