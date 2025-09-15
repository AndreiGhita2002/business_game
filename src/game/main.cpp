//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"
#include "raylib-cpp.hpp"
#include "VoxelMesher.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif


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
    global::model = static_cast<Model *>(malloc(sizeof(Model)));
}

void global::shutdown() {
    delete game_map;
    UnloadModel(*model);
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
    // Only updates chunk 0,0
    //todo do other chunks
    std::vector<Int2> chunks;
    chunks.emplace_back(Int2{0, 0});

    for (Int2 chunk_pos : chunks) {
        if (game_map->chunkWasUpdated[chunk_pos]) {
            VoxelChunk* chunk = game_map->get_chunk(chunk_pos);

            Mesh mesh = build_chunk_mesh(*chunk, Vector3{0, 0, 0}, 1.0f);
            *global::model = LoadModelFromMesh(mesh);

            game_map->chunkWasUpdated[chunk_pos] = false;
        }
    }
}

void global::mainLoop() {
    const Vector3 boxPos = { -(game_map->size_x / 2.0f), 0.0f, -(game_map->size_y / 2.0f) };
    const Vector3 boxSize = { 1.0f, 1.0f, 1.0f };

    // Update
    updateCamera();
    updateVoxelMesh();

    // Draw
    BeginDrawing();
    {
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        {
            // Only draws chunk 0,0
            DrawModel(*global::model, {0,0,0}, 1.0f, GREEN);
            DrawModelWires(*global::model, {0,0,0}, 1.0f, DARKGRAY);
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
