//
// Created by Andrei Ghita on 01.09.2025.
//

#include "VoxelMap.hpp"
#include "PerlinNoise.hpp"

VoxelMap::VoxelMap(const uint32_t size_x, const uint32_t size_y) {
    this->size_x = size_x;
    this->size_y = size_y;
    this->chunks_x = size_x / 16 + (size_x % 16 ? 1 : 0);
    this->chunks_y = size_x / 16 + (size_x % 16 ? 1 : 0);

    this->voxelColourMap = std::map<VoxelID, Color>();
    voxelColourMap.insert(std::pair<VoxelID, Color>(0, RED)); // air, should not be seen
    voxelColourMap.insert(std::pair<VoxelID, Color>(1, BEIGE));
    voxelColourMap.insert(std::pair<VoxelID, Color>(2, DARKGREEN));

    this->chunkMap = std::map<Int2, VoxelChunk>();
    for (int ix = 0; ix < chunks_x; ++ix) {
        for (int iy = 0; iy < chunks_y; ++iy) {
            chunkMap[Int2(ix, iy)] = VoxelChunk{};
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
            auto v = get_voxel(Int3(ix, iy, j));
            *v = voxel_type;
        }
    }
}

VoxelMap::~VoxelMap() {}

VoxelMap::VoxelChunk* VoxelMap::get_chunk(Int2 pos) {
    // finding the chunk
    const int cx = floordiv(pos.x, CHUNK_SIZE);
    const int cy = floordiv(pos.y, CHUNK_SIZE);

    auto pair = chunkMap.find({cx, cy});

    if (pair == chunkMap.end()) return nullptr;
    else return &pair->second;
}

VoxelMap::VoxelID* VoxelMap::get_voxel(Int3 pos) {
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

VoxelMap::VoxelID* VoxelMap::get_chunk_voxel(VoxelChunk& chunk, const Int3 pos) {
    return &chunk[pos.x
        + pos.y * CHUNK_SIZE
        + pos.z * CHUNK_SIZE * CHUNK_SIZE];
}
