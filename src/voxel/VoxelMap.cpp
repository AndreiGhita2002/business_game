//
// Created by Andrei Ghita on 01.09.2025.
//

#include "voxel/VoxelMap.hpp"

#include "PerlinNoise.hpp"
#include <raylib-cpp.hpp>
#include <istream>
#include <ostream>

#include "voxel/VoxelMesher.hpp"
#include "game/main.hpp"

VoxelMap::VoxelMap(VoxelView* view, const uint32_t size_x, const uint32_t size_y,
                   const bool generate_terrain)
    : VoxelGrid(view)
{
    this->size = Int2(size_x, size_y);
    this->chunk_count = Int2(
        size_x / 16 + (size_x % 16 ? 1 : 0),
        size_y / 16 + (size_y % 16 ? 1 : 0));

    this->transform = identity();

    //TODO (optimisation) colorMaps should be shared between grids, somewhere global
    this->voxel_colours = std::make_shared<std::map<VoxelID, Color>>();
    auto colorMap = this->voxel_colours.get();
    colorMap->insert(std::pair<VoxelID, Color>(0, RED)); // air, should not be seen
    colorMap->insert(std::pair<VoxelID, Color>(1, BEIGE));
    colorMap->insert(std::pair<VoxelID, Color>(2, DARKGREEN));
    colorMap->insert(std::pair<VoxelID, Color>(3, YELLOW));
    // Not used by the terrain, these are here to fill out the editor palette
    colorMap->insert(std::pair<VoxelID, Color>(4, BLUE));
    colorMap->insert(std::pair<VoxelID, Color>(5, ORANGE));
    colorMap->insert(std::pair<VoxelID, Color>(6, PURPLE));
    colorMap->insert(std::pair<VoxelID, Color>(7, BROWN));
    colorMap->insert(std::pair<VoxelID, Color>(8, DARKGRAY));
    colorMap->insert(std::pair<VoxelID, Color>(9, SKYBLUE));
    colorMap->insert(std::pair<VoxelID, Color>(10, MAROON));
    colorMap->insert(std::pair<VoxelID, Color>(11, RAYWHITE));

    this->chunks = std::map<Int2, VoxelChunk>();
    for (int ix = 0; ix < chunk_count.x; ++ix) {
        for (int iy = 0; iy < chunk_count.y; ++iy) {
            chunks[Int2(ix, iy)] = VoxelChunk{};
            chunk_was_updated[Int2(ix, iy)] = true;
        }
    }

    // A map that is about to be read out of a file keeps its chunks as air
    if (!generate_terrain) return;

    const siv::PerlinNoise::seed_type seed = 123456u;
    const siv::PerlinNoise perlin{ seed };

    for (int i = 0; i < size_x * size_y; i++) {
        auto ix = i % size_x, iy = i / size_x;

        // Perlin Noise Generation
        float noise = perlin.noise2D(ix * 0.05, iy * 0.05) * CHUNK_SIZE;
        int height = std::clamp(static_cast<int>(noise), 0, CHUNK_SIZE - 1);

        // Lift the edges to see the clear limit of the chunks
        // auto cx = ix % 16, cy = iy % 16;
        // bool is_edge = cx == 0 || cy == 0;// || cx == CHUNK_SIZE -2 || cy == CHUNK_SIZE - 2;
        // int height = is_edge ? 3 : 1;

        for (int j = 0; j <= height; j++) {
            VoxelID voxel_type = j < 3 ? 1 : 2;
            auto v = VoxelMap::get_voxel(Int3(ix, iy, j));
            *v = voxel_type;
        }
    }
}

VoxelMap::~VoxelMap() {
    for (auto it = chunk_models.begin(); it != chunk_models.end(); ++it) {
        unload_chunk_model(it->second.model);
    }
    chunk_models.clear();
}

std::string & VoxelMap::get_grid_type() {
    static std::string TYPE = VOXEL_MAP_STR;
    return TYPE;
}

bool VoxelMap::write_body(std::ostream& out) {
    voxel_file::write_i32(out, size.x);
    voxel_file::write_i32(out, size.y);
    voxel_file::write_u32(out, static_cast<uint32_t>(chunks.size()));

    for (const auto& [chunk_pos, chunk] : chunks) {
        voxel_file::write_i32(out, chunk_pos.x);
        voxel_file::write_i32(out, chunk_pos.y);
        // The voxels go out in the shared format, the same one a single chunk
        // grid uses, so only the chunk grid around them is particular to a map
        if (!voxel_file::write_chunk(out, chunk)) return false;
    }
    return out.good();
}

VoxelGrid* VoxelMap::load_body(std::istream& in, const voxel_file::LoadContext& ctx) {
    int32_t size_x = 0, size_y = 0;
    uint32_t chunk_count = 0;
    if (!voxel_file::read_i32(in, &size_x) ||
        !voxel_file::read_i32(in, &size_y) ||
        !voxel_file::read_u32(in, &chunk_count))
        return nullptr;

    if (size_x <= 0 || size_y <= 0) {
        TraceLog(LOG_WARNING, "VOXELMAP: file asks for a %i by %i map", size_x, size_y);
        return nullptr;
    }
    // A corrupt count would otherwise send the loop below allocating chunks
    // until the read finally fails
    const uint32_t chunks_x = size_x / CHUNK_SIZE + (size_x % CHUNK_SIZE ? 1 : 0);
    const uint32_t chunks_y = size_y / CHUNK_SIZE + (size_y % CHUNK_SIZE ? 1 : 0);
    if (chunk_count > chunks_x * chunks_y) {
        TraceLog(LOG_WARNING, "VOXELMAP: file holds %u chunks, a %i by %i map has room for %u",
                 chunk_count, size_x, size_y, chunks_x * chunks_y);
        return nullptr;
    }

    // The chunks are read into a map that already holds air, so a file that
    // leaves some of them out still gives a complete grid
    auto* map = new VoxelMap(ctx.view, static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y), false);
    if (ctx.palette) map->voxel_colours = ctx.palette;

    for (uint32_t i = 0; i < chunk_count; ++i) {
        int32_t chunk_x = 0, chunk_y = 0;
        if (!voxel_file::read_i32(in, &chunk_x) || !voxel_file::read_i32(in, &chunk_y)) {
            delete map;
            return nullptr;
        }
        const Int2 chunk_pos{chunk_x, chunk_y};
        if (!voxel_file::read_chunk(in, &map->chunks[chunk_pos])) {
            delete map;
            return nullptr;
        }
        map->chunk_was_updated[chunk_pos] = true;
    }
    return map;
}

void VoxelMap::update_models() {
    // Where the map itself sits, its own transform with every parent's on top.
    // The same for every chunk, so it is only worked out once.
    const Transform world = get_world_transform();

    for (auto it = chunks.begin(); it != chunks.end(); ++it) {
        auto chunk_pos = it->first;
        auto chunk = &it->second;
        auto chunk_model = chunk_models.find(chunk_pos);

        // Where the chunk sits inside the map. Chunk (cx, cy) holds the global
        // columns 16cx to 16cx+15, and a voxel spans one unit, so chunks sit
        // CHUNK_SIZE apart and meet exactly. A smaller spacing would overlap
        // them and draw two different columns of terrain in the same place.
        // What the map is doing is left out of this on purpose: it is applied
        // on top by voxel_model_matrix() when the chunk is drawn, so that a
        // moved map does not need remeshing.
        auto model_transform = identity();
        model_transform.translation = Vector3{
            static_cast<float>(chunk_pos.x) * CHUNK_SIZE,
            0.0,
            static_cast<float>(chunk_pos.y) * CHUNK_SIZE
        };

        // render distance check, which is a question about the world and so
        // needs the chunk carried out of the map's space first
        if (chunk_model != chunk_models.end() && global::limit_render_distance) {
            const Transform chunk_world = transform_transform(model_transform, world);
            chunk_model->second.do_render = view->isInRenderDistance(chunk_world.translation);
        }

        if (chunk_was_updated[chunk_pos]) {
            auto meshes = build_chunk_mesh(*chunk, Vector3{0.0,0.0,0.0}, 1.0f);
            auto new_model = build_chunk_model(meshes, *voxel_colours);

            // A chunk that is meshed again already holds a model, which would
            // leak its GPU buffers if it were simply overwritten. This happens
            // on every voxel the editor places.
            if (chunk_model != chunk_models.end())
                unload_chunk_model(chunk_model->second.model);

            chunk_models[chunk_pos] = ModelInfo{true, new_model, model_transform};
            chunk_was_updated[chunk_pos] = false;
        }
    }
}

std::vector<ModelInfo*> VoxelMap::get_models() {
    auto out = std::vector<ModelInfo*>{};
    for (auto it = chunk_models.begin(); it != chunk_models.end(); ++it) {
        if (it->second.do_render) {
            out.emplace_back(&it->second);
        }
    }
    return out;
}

bool VoxelMap::set_voxel(const Int3 grid_pos, const VoxelID id) {
    // get_voxel() wraps out of range coordinates instead of rejecting them,
    // so the bounds are checked before it is called
    if (grid_pos.x < 0 || grid_pos.x >= size.x ||
        grid_pos.y < 0 || grid_pos.y >= size.y ||
        grid_pos.z < 0 || grid_pos.z >= CHUNK_SIZE)
        return false;

    VoxelID* voxel = get_voxel(grid_pos);
    if (voxel == nullptr) return false;

    *voxel = id;
    chunk_was_updated[Int2{floordiv(grid_pos.x, CHUNK_SIZE), floordiv(grid_pos.y, CHUNK_SIZE)}] = true;
    return true;
}

bool VoxelMap::model_to_grid(const ModelInfo* model, const Vector3 local_pos, Int3* out) {
    if (model == nullptr) return false;

    // Each chunk is meshed at its own origin, so the local position only gives
    // the voxel inside the chunk. The chunk itself is found by identity, as
    // ModelInfo does not carry which chunk it was built from.
    for (const auto& [chunk_pos, info] : chunk_models) {
        if (&info != model) continue;

        // Model space is (x, z, y) in grid terms
        *out = Int3{
            chunk_pos.x * CHUNK_SIZE + static_cast<int>(floorf(local_pos.x)),
            chunk_pos.y * CHUNK_SIZE + static_cast<int>(floorf(local_pos.z)),
            static_cast<int>(floorf(local_pos.y)),
        };
        return true;
    }
    return false;
}

VoxelChunk* VoxelMap::get_chunk(Int2 pos) {
    // finding the chunk
    const int cx = floordiv(pos.x, CHUNK_SIZE);
    const int cy = floordiv(pos.y, CHUNK_SIZE);

    auto pair = chunks.find({cx, cy});

    if (pair == chunks.end()) return nullptr;
    else return &pair->second;
}

VoxelID* VoxelMap::get_voxel(Int3 pos) {
    // finding the chunk
    auto chunk = get_chunk({pos.x, pos.y});
    if (chunk == nullptr) return nullptr;

    // getting the voxel inside the chunk
    Int3 chunk_pos = {
        floormod(pos.x, CHUNK_SIZE),
        floormod(pos.y, CHUNK_SIZE),
        floormod(pos.z, CHUNK_SIZE),
    };
    return get_chunk_voxel(*chunk, chunk_pos);
}

VoxelID* VoxelMap::get_chunk_voxel(VoxelChunk& chunk, const Int3 pos) {
    return &chunk[pos.x
        + pos.y * CHUNK_SIZE
        + pos.z * CHUNK_SIZE * CHUNK_SIZE];
}

Int2 VoxelMap::get_size() {
    return size;
}

Int2 VoxelMap::get_chunk_count() const {
    return chunk_count;
}
