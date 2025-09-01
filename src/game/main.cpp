//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"
#include "raylib-cpp.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif


void global::init() {
    // window = raylib::Window(800, 450, "business game");
    window.Init(800, 450, "business game");
    state.camera = {
        {
            { 0.0f, 10.0f, 10.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            45.0f,
            0
        },
    };
}

void global::shutdown() {
    window.Close();
}

void global::mainLoop() {
    // Update
    Vector3 boxPos = { -4.0f, 1.0f, 0.0f };
    Vector3 boxSize = { 2.0f, 2.0f, 2.0f };

    // Draw
    BeginDrawing();
    {
        ClearBackground(RAYWHITE);
        BeginMode3D(global::state.camera);
        {
            DrawCube(boxPos, boxSize.x, boxSize.y, boxSize.z, GRAY);
            DrawCubeWires(boxPos, boxSize.x, boxSize.y, boxSize.z, DARKGRAY);
        }
        EndMode3D();
        DrawText("Congrats! You created your first raylib-cpp window!", 160, 200, 20, LIGHTGRAY);
    }
    EndDrawing();
}

int main() {
    global::init();

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(global::mainLoop(), 0, 1);
#else
    SetTargetFPS(60);

    while (!raylib::Window::ShouldClose()) {
        global::mainLoop();
    }
#endif
    global::shutdown();
    return 0;
}
