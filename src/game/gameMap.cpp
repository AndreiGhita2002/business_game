//
// Created by Andrei Ghita on 01.09.2025.
//

#include "gameMap.hpp"

GameMap::GameMap(const uint32_t size_x, const uint32_t size_y) {
    this->size_x = size_x;
    this->size_y = size_y;

    this->terrain_elevation = new uint8_t[size_x * size_y / 4]{0};
    this->build_tiles = new BuildTileType[size_x * size_y]{Flat};
}

GameMap::~GameMap() {
    delete[] terrain_elevation;
    delete[] build_tiles;
}

uint8_t GameMap::get_terrain_elevation(const uint32_t x, const uint32_t y) const {
    return terrain_elevation[y * (size_x / 2) + x];
}

uint8_t GameMap::get_build_elevation(const uint32_t x, const uint32_t y) const {
    return get_terrain_elevation(x / 2, y / 2);
}

BuildTileType GameMap::get_build_tile_type(const uint32_t x, const uint32_t y) const {
    return build_tiles[y * size_x + x];
}
