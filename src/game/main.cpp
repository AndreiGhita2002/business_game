//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"
#include "raylib-cpp.hpp"
#include "VoxelMesher.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif


Vector3 apply_transform(Vector3 v, Transform t) {
    // Scale
    Vector3 scaled = {
        v.x * t.scale.x,
        v.y * t.scale.y,
        v.z * t.scale.z
    };

    // Rotate
    Vector3 rotated = Vector3RotateByQuaternion(scaled, t.rotation);

    // Translate
    return Vector3Add(rotated, t.translation);
}

void global::init() {
    // window = raylib::Window(800, 450, "business game");
    raylib::Window::Init(1600, 900, "business game");
    camera = {
        {
            { 15.0f, 15.0f, 15.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            45.0f,
            0
        },
    };
    game_map = new VoxelMap(128, 128);
    chunk_models = std::map<Int2, Model>();
}

void global::shutdown() {
    for (auto it = chunk_models.begin(); it != chunk_models.end(); ++it) {
        UnloadModel(it->second);
    }
    chunk_models.clear();
    delete game_map;
    raylib::Window::Close();
}

void global::updateCamera() {
    const float moveSpeed = 12.0f * GetFrameTime();
    const float panSpeed  = 3.0f * GetFrameTime();

    // --- Build camera-relative basis on the XZ plane ---
    float dx = camera.target.x - camera.position.x;
    float dz = camera.target.z - camera.position.z;

    // Forward (XZ only)
    float fLen = sqrtf(dx*dx + dz*dz);
    if (fLen < 1e-6f) {
        // degenerate: point some default forward to avoid NaNs
        dx = 0.0f; dz = -1.0f; fLen = 1.0f;
    }
    float fx = dx / fLen;
    float fz = dz / fLen;

    // Right (perpendicular on XZ): rotate forward 90° clockwise around Y
    float rx =  fz;
    float rz = -fx;

    // --- Input to forward/strafe amounts ---
    float fwd = 0.0f, strafe = 0.0f;
    if (IsKeyDown(KEY_W)) fwd += 1.0f;
    if (IsKeyDown(KEY_S)) fwd -= 1.0f;
    if (IsKeyDown(KEY_A)) strafe += 1.0f;
    if (IsKeyDown(KEY_D)) strafe -= 1.0f;

    // Combine and normalize so diagonals aren’t faster
    float mx = fx * fwd + rx * strafe;
    float mz = fz * fwd + rz * strafe;
    float mLen = sqrtf(mx*mx + mz*mz);
    if (mLen > 1e-6f) {
        mx = (mx / mLen) * moveSpeed;
        mz = (mz / mLen) * moveSpeed;

        camera.position.x += mx;
        camera.position.z += mz;
        camera.target.x   += mx;
        camera.target.z   += mz;
    }

    // --- Panning (yaw around position) ---
    if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_E)) {
        float angle = IsKeyDown(KEY_Q) ? -panSpeed : panSpeed;

        float cosA = cosf(angle);
        float sinA = sinf(angle);

        float tdx = camera.target.x - camera.position.x;
        float tdz = camera.target.z - camera.position.z;

        float ndx = tdx * cosA - tdz * sinA;
        float ndz = tdx * sinA + tdz * cosA;

        camera.target.x = camera.position.x + ndx;
        camera.target.z = camera.position.z + ndz;
    }

    // --- Vertical movement ---
    if (IsKeyDown(KEY_F)) {
        camera.position.y += moveSpeed;
        camera.target.y   += moveSpeed;
    }
    if (IsKeyDown(KEY_C)) {
        camera.position.y -= moveSpeed;
        camera.target.y   -= moveSpeed;
    }
}

void global::updateVoxelMesh() {
    for (auto it = game_map->chunkMap.begin(); it != game_map->chunkMap.end(); ++it) {
        auto chunk_pos = it->first;
        auto chunk = &it->second;

        // todo check if chunk is within range of the camera

        if (game_map->chunkWasUpdated[chunk_pos]) {
            auto chunkOrigin = Vector3{
                static_cast<float>(chunk_pos.x) - 0.5f,
                0.0,
                static_cast<float>(chunk_pos.y) - 0.5f
            };
            auto meshes = build_chunk_mesh(*chunk, chunkOrigin, 1.0f);
            auto model = build_chunk_model(meshes, game_map->voxelColourMap);
            chunk_models[chunk_pos] = model;

            game_map->chunkWasUpdated[chunk_pos] = false;
        }
    }
}

void global::mainLoop() {
    // Update
    updateCamera();
    updateVoxelMesh();

    // Draw
    BeginDrawing();
    {
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        {
            for (auto it = chunk_models.begin(); it != chunk_models.end(); ++it) {
                auto chunk_pos = Vector3{
                    static_cast<float>(it->first.x) * (CHUNK_SIZE - 1),
                    0.0,
                    static_cast<float>(it->first.y) * (CHUNK_SIZE - 1)
                };
                Vector3 world_pos = apply_transform(chunk_pos, game_map->world_transform);
                auto model = it->second;

                DrawModel(model, world_pos, 1.0f, WHITE);
                DrawModelWires(model, world_pos, 1.0f, DARKGRAY);
            }

            DrawCube(Vector3{0.0, 5.0, 0.0}, 1.0, 1.0, 1.0, ORANGE);
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
