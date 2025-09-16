//
// Created by Andrei Ghita on 01.09.2025.
//

#include "voxel/VoxelMap.hpp"

#include <raymath.h>

#include "voxel/VoxelMesher.hpp"
#include "PerlinNoise.hpp"
#include "game/main.hpp"

VoxelMap::VoxelMap(const uint32_t size_x, const uint32_t size_y) {
    this->size = Int2(size_x, size_y);
    this->chunk_count = Int2(
        size_x / 16 + (size_x % 16 ? 1 : 0),
        size_y / 16 + (size_y % 16 ? 1 : 0));

    this->transform = identity();

    this->voxel_colours = std::make_shared<std::map<VoxelID, Color>>();
    auto colorMap = this->voxel_colours.get();
    colorMap->insert(std::pair<VoxelID, Color>(0, RED)); // air, should not be seen
    colorMap->insert(std::pair<VoxelID, Color>(1, BEIGE));
    colorMap->insert(std::pair<VoxelID, Color>(2, DARKGREEN));
    colorMap->insert(std::pair<VoxelID, Color>(3, YELLOW));

    this->chunks = std::map<Int2, VoxelChunk>();
    for (int ix = 0; ix < chunk_count.x; ++ix) {
        for (int iy = 0; iy < chunk_count.y; ++iy) {
            chunks[Int2(ix, iy)] = VoxelChunk{};
            chunk_was_updated[Int2(ix, iy)] = true;
        }
    }

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
        UnloadModel(it->second.model);
    }
    chunk_models.clear();
}

void VoxelMap::update_models() {
    for (auto it = chunks.begin(); it != chunks.end(); ++it) {
        auto chunk_pos = it->first;
        auto chunk = &it->second;
        auto chunk_model = chunk_models.find(chunk_pos);

        // calculating the position of the chunk in render space
        auto offset = Vector3{
            static_cast<float>(it->first.x) * (CHUNK_SIZE - 1),
            0.0,
            static_cast<float>(it->first.y) * (CHUNK_SIZE - 1)
        };
        offset = apply_transform(offset, transform);

        // render distance check
        if (chunk_model != chunk_models.end() && global::limit_render_distance) {
            chunk_model->second.do_render = global::isInRenderDistance(offset);
        }

        if (chunk_was_updated[chunk_pos]) {
            auto meshes = build_chunk_mesh(*chunk, Vector3{0.0,0.0,0.0}, 1.0f);
            auto new_model = build_chunk_model(meshes, *voxel_colours);

            chunk_models[chunk_pos] = ModelInfo{true, new_model, offset};
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
