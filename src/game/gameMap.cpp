//
// Created by Andrei Ghita on 01.09.2025.
//

#include "gameMap.hpp"
#include "PerlinNoise.hpp"
#include <cmath>

GameMap::GameMap(const uint32_t size_x, const uint32_t size_y) {
    this->size_x = size_x;
    this->size_y = size_y;

    const siv::PerlinNoise::seed_type seed = 123456u;
    const siv::PerlinNoise perlin{ seed };

    this->terrain_elevation = new uint8_t[size_x * size_y]{0};
    this->terrain_types = new VOXEL_TYPE[size_x * size_y]{0};

    for (int i = 0; i < size_x * size_y; i++) {
        float noise = perlin.noise2D((i % size_x) * 0.1, (i / size_x) * 0.1) * 8;
        this->terrain_elevation[i] = abs(noise);
    }
}

GameMap::~GameMap() {
    delete[] terrain_elevation;
    delete[] terrain_types;
}

uint8_t GameMap::get_terrain_elevation(const uint32_t x, const uint32_t y) const {
    return terrain_elevation[y * size_x + x];
}

VOXEL_TYPE GameMap::get_terrain_type(const uint32_t x, const uint32_t y) const {
    return terrain_types[y * size_x + x];
}
