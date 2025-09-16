//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"
#include "raylib-cpp.hpp"
#include "voxel/VoxelMesher.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif


Vector3 apply_transform(const Vector3 v, const Transform &t) {
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

bool global::isInRenderDistance(const Vector3 v) {
    return Vector3Distance(camera.position, v) <= render_distance
    || !limit_render_distance;
}

void global::init() {
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

    single_chunk_grid = new SingleChunkGrid(game_map->voxel_colours);
    *single_chunk_grid->get_voxel(Int3(0.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(1.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(2.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(3.0,0.0,0.0)) = 3;
    single_chunk_grid->transform.translation = Vector3(-2.0f, 6.0f, -2.0f);
    single_chunk_grid->transform.scale = Vector3(2.0f, 2.0f, 2.0f);
    single_chunk_grid->was_updated = true;
}

void global::shutdown() {
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
    game_map->update_models();
    single_chunk_grid->update_models();
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
            // Voxel Map
            for (ModelInfo* model_info : game_map->get_models()) {
                DrawModel(model_info->model, model_info->transform.translation, 1.0f, WHITE);
                DrawModelWires(model_info->model, model_info->transform.translation, 1.0f, DARKGRAY);
            }

            // Single Chunk Grid
            if (auto model_info = single_chunk_grid->get_models().front()) {

                auto axis = Vector3{};
                auto angle = 0.0f;
                QuaternionToAxisAngle(model_info->transform.rotation, &axis, &angle);

                DrawModelEx(model_info->model, model_info->transform.translation,
                    axis, angle,
                    model_info->transform.scale, WHITE);
                DrawModelWiresEx(model_info->model, model_info->transform.translation,
                    axis, angle,
                    model_info->transform.scale, DARKGRAY);
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
