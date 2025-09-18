//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"

#include <iostream>

#include "raylib-cpp.hpp"
#include "voxel/VoxelMesher.hpp"
#include "voxel/SingleChunkGrid.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

#define GLSL_VERSION 330

Light& Light::create(LightType t, Vector3 pos, Vector3 tgt, Color col, const Shader& shader) {
    // Emplace a default Light in the vector and fill it in-place.
    Light& L = global::lights.emplace_back();

    L.enabled = true;
    L.type = static_cast<int>(t);
    L.position = pos;
    L.target = tgt;
    L.color = col;

    L.id = global::next_light_id++;
    // NOTE: uniform names must match your shader
    L.enabled_loc  = GetShaderLocation(shader, TextFormat("lights[%i].enabled",  L.id));
    L.type_loc     = GetShaderLocation(shader, TextFormat("lights[%i].type",     L.id));
    L.position_loc = GetShaderLocation(shader, TextFormat("lights[%i].position", L.id));
    L.target_loc   = GetShaderLocation(shader, TextFormat("lights[%i].target",   L.id));
    L.color_loc    = GetShaderLocation(shader, TextFormat("lights[%i].color",    L.id));
    // If you actually use attenuation in the shader, set it too:
    // L.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", L.id));

    return L;
}

void Light::update(Shader shader) const {
    // Send to shader light enabled state and type
    int s_enabled = enabled ? 1 : 0;
    SetShaderValue(shader, enabled_loc, &s_enabled, SHADER_UNIFORM_INT);
    int s_type = (type == POINT_LIGHT) ? 1 : 0;
    SetShaderValue(shader, type_loc, &s_type, SHADER_UNIFORM_INT);

    // Send to shader light position values
    float s_position[3] = {position.x, position.y, position.z};
    SetShaderValue(shader, position_loc, s_position, SHADER_UNIFORM_VEC3);

    // Send to shader light target position values
    float s_target[3] = {target.x, target.y, target.z};
    SetShaderValue(shader, target_loc, s_target, SHADER_UNIFORM_VEC3);

    // Send to shader light color values
    Vector4 s_color = { color.r/255.f, color.g/255.f, color.b/255.f, color.a/255.f };
    SetShaderValue(shader, color_loc, &s_color, SHADER_UNIFORM_VEC4);
}

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
    // TODO (optimisation) this should be rewritten so that it doesn't use a sqrt operation
    return Vector3Distance(camera.position, v) <= render_distance
    || !limit_render_distance;
}

void global::init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);  // Enable Multi Sampling Anti Aliasing 4x (if available)
    raylib::Window::Init(1600, 900, "business game");
    camera = {
        {
            { 10.0f, 5.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            45.0f,
            0
        },
    };

    voxel_shader = LoadShader("../resources/shaders/lighting.vs", "../resources/shaders/lighting.fs");
    // Get some required shader locations
    voxel_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(voxel_shader, "viewPos");
    // NOTE: "matModel" location name is automatically assigned on shader loading,
    // no need to get the location again if using that uniform name
    //shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");

    // Ambient light level (some basic lighting)
    int ambientLoc = GetShaderLocation(voxel_shader, "ambient");
    SetShaderValue(voxel_shader, ambientLoc, (float[4]){0.01f, 0.01f, 0.01f, 1.0f}, SHADER_UNIFORM_VEC4);

    // Create lights
    lights = std::vector<Light>();
    auto light_pos = Vector3Scale(Vector3{15.0, 6.0, 15.0}, global::voxel_scale);
    sun_light = &Light::create(POINT_LIGHT, light_pos, Vector3Zero(), WHITE, voxel_shader);

    // Voxels
    voxel_grids = std::vector<VoxelGrid*>();

    game_map = new VoxelMap(128, 128);
    voxel_grids.emplace_back(game_map);

    auto single_chunk_grid = new SingleChunkGrid(game_map->voxel_colours);
    *single_chunk_grid->get_voxel(Int3(0.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(1.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(2.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(3.0,0.0,0.0)) = 3;
    single_chunk_grid->transform.translation = Vector3(-2.0f, 6.0f, -2.0f);
    single_chunk_grid->transform.scale = Vector3(2.0f, 2.0f, 2.0f);
    single_chunk_grid->was_updated = true;
    voxel_grids.emplace_back(single_chunk_grid);
}

void global::shutdown() {
    // TODO: this function should be called, but it produces a double free
    //  figure out how to call it without the error
    // UnloadShader(shader);
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

    // --- Shader Update ---
    float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    SetShaderValue(voxel_shader, voxel_shader.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void global::updateLights() {
    for (Light &light : lights) {
        light.update(voxel_shader);
    }
}

void global::updateVoxelMesh() {
    for (VoxelGrid* grid : voxel_grids) {
        grid->update_models();
    }
}

void global::mainLoop() {
    // Update
    updateCamera();
    updateVoxelMesh();
    updateLights();

    // Draw
    BeginDrawing();
    {
        ClearBackground(RAYWHITE);
        BeginMode3D(camera);
        {
            for (VoxelGrid* grid : voxel_grids) {
                for (ModelInfo* model_info : grid->get_models()) {
                    // Offset
                    auto offset = Vector3Scale(model_info->transform.translation, voxel_scale);

                    // Rotation
                    auto axis = Vector3{};
                    auto angle = 0.0f;
                    QuaternionToAxisAngle(model_info->transform.rotation, &axis, &angle);

                    // Scale
                    auto scale = Vector3Scale(model_info->transform.scale, voxel_scale);

                    // Drawing the model
                    DrawModelEx(model_info->model, offset,
                        axis, angle, scale, WHITE);

                    // Drawing wires
                    // DrawModelWiresEx(model_info->model, offset,
                    //     axis, angle, scale, DARKGRAY);
                }
            }

            // Shader Mode is only necessary for immediate draw calls
            BeginShaderMode(voxel_shader);
            {
                // Test Cube
                DrawCube(Vector3{0.0, 0.0, 0.0}, 1.0, 1.0, 1.0, ORANGE);
            }
            EndShaderMode();

            // Draw spheres to show where the lights are
            for (Light light : lights) {
                if (light.enabled) DrawSphereEx(light.position, 0.2f, 8, 8, light.color);
                else DrawSphereWires(light.position, 0.2f, 8, 8, ColorAlpha(light.color, 0.3f));
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
