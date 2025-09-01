//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include "gameMap.hpp"

namespace global {
    inline raylib::Camera camera;
    inline GameMap* game_map;

    static void init();
    static void mainLoop();
    static void shutdown();
}


#endif //BUSINESS_GAME_MAIN_HPP