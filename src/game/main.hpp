//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include <Window.hpp>

namespace global {
    inline bool initialised = false;
    raylib::Window window;
    inline struct State {
        raylib::Camera camera;
    } state;

    static void init();
    static void mainLoop();
    static void shutdown();
}


#endif //BUSINESS_GAME_MAIN_HPP