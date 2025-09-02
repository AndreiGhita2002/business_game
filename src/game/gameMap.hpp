//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_GAMEMAP_HPP
#define BUSINESS_GAME_GAMEMAP_HPP
#include <cstdint>

#define VOXEL_TYPE uint8_t

class GameMap {

public:
    uint32_t size_x, size_y;

    // Terrain data
    uint8_t* terrain_elevation;
    VOXEL_TYPE* terrain_types;

    GameMap(uint32_t size_x, uint32_t size_y);
    ~GameMap();

    uint8_t get_terrain_elevation(uint32_t x, uint32_t y) const;
    VOXEL_TYPE get_terrain_type(uint32_t x, uint32_t y) const;
};


#endif //BUSINESS_GAME_GAMEMAP_HPP