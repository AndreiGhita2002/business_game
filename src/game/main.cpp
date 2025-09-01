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
    raylib::Window::Init(800, 450, "business game");
    camera = {
        {
            { 5.0f, 5.0f, 5.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            45.0f,
            0
        },
    };
    game_map = new GameMap(8, 8);
    game_map->terrain_elevation[0]++;
}

void global::shutdown() {
    delete game_map;
    raylib::Window::Close();
}

void global::updateCamera() {
    const float moveSpeed = 0.2f;
    const float panSpeed  = 0.05f; // radians per frame

    // X/Z movement
    if (IsKeyDown(KEY_S)) {
        camera.position.x += moveSpeed;
        camera.position.z += moveSpeed;
        camera.target.x   += moveSpeed;
        camera.target.z   += moveSpeed;
    }
    else if (IsKeyDown(KEY_W)) {
        camera.position.x -= moveSpeed;
        camera.position.z -= moveSpeed;
        camera.target.x   -= moveSpeed;
        camera.target.z   -= moveSpeed;
    }
    else if (IsKeyDown(KEY_A)) {
        camera.position.x -= moveSpeed;
        camera.position.z += moveSpeed;
        camera.target.x   -= moveSpeed;
        camera.target.z   += moveSpeed;
    }
    else if (IsKeyDown(KEY_D)) {
        camera.position.x += moveSpeed;
        camera.position.z -= moveSpeed;
        camera.target.x   += moveSpeed;
        camera.target.z   -= moveSpeed;
    }

    // Panning
    if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_E)) {
        float angle = (IsKeyDown(KEY_Q) ? -panSpeed : panSpeed);

        // Vector from camera to target
        float dx = camera.target.x - camera.position.x;
        float dz = camera.target.z - camera.position.z;

        // Apply 2D rotation around Y axis
        float cosA = cosf(angle);
        float sinA = sinf(angle);

        float newDx = dx * cosA - dz * sinA;
        float newDz = dx * sinA + dz * cosA;

        camera.target.x = camera.position.x + newDx;
        camera.target.z = camera.position.z + newDz;
    }

    // Vertical movement
    if (IsKeyDown(KEY_F)) {
        camera.position.y += moveSpeed;
        camera.target.y   += moveSpeed;
    }
    else if (IsKeyDown(KEY_C)) {
        camera.position.y -= moveSpeed;
        camera.target.y   -= moveSpeed;
    }
}



void global::mainLoop() {
    const Vector3 boxPos = { -(game_map->size_x / 2.0f), 0.0f, -(game_map->size_y / 2.0f) };
    const Vector3 boxSize = { 1.0f, 1.0f, 1.0f };

    // Update
    updateCamera();

    // Draw
    BeginDrawing();
    {
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        {
            for (auto iy = 0; iy < game_map->size_y; ++iy) {
                for (auto ix = 0; ix < game_map->size_x; ++ix) {
                    auto height = game_map->get_build_elevation(ix, iy);
                    Vector3 pos = {boxPos.x + ix, boxPos.y + height, boxPos.z + iy};
                    auto col = (ix + (iy % 4 >= 2 ? 2 : 0)) % 4 >= 2 ? GRAY : RED;
                    DrawCube(pos, boxSize.x, boxSize.y, boxSize.z, col);
                    DrawCubeWires(pos, boxSize.x, boxSize.y, boxSize.z, BLACK);
                }
            }
        }
        EndMode3D();
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
