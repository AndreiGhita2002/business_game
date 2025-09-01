//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_GAMEMAP_HPP
#define BUSINESS_GAME_GAMEMAP_HPP
#include <cstdint>

// Slope naming conventions:
//  Slope[+y][-y][+x][-y]
enum BuildTileType {
    Flat, Air,
    Slope0, Slope1, Slope2, Slope3,
    Slope4, Slope5, Slope6, Slope7,
    Slope8, Slope9, Slope10, Slope11,
    Slope12, Slope13, Slope14, Slope15
};


class GameMap {

public:
    // Must be even!
    uint32_t size_x, size_y;

    //Terrain Elevation Grid
    // array size is [(size_x * size_y) / 4]
    uint8_t* terrain_elevation;

    //Build Grid - subgrid of the terrain grid
    // array size is [(size_x * size_y) / 2]
    BuildTileType* build_tiles;

    GameMap(uint32_t size_x, uint32_t size_y);
    ~GameMap();

    uint8_t get_terrain_elevation(uint32_t x, uint32_t y) const;
    uint8_t get_build_elevation(uint32_t x, uint32_t y) const;
    BuildTileType get_build_tile_type(uint32_t x, uint32_t y) const;
};


#endif //BUSINESS_GAME_GAMEMAP_HPP