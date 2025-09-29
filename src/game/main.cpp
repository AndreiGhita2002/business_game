//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <regex>

#include "raylib-cpp.hpp"
#include "voxel/VoxelMesher.hpp"
#include "voxel/SingleChunkGrid.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

#define GLSL_VERSION 330

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

    // voxel_shader = LoadShader("../resources/shaders/lighting.vs", "../resources/shaders/lighting.fs");
    voxel_shader = loadAndPatchShader("../resources/shaders/lighting", 2);
    voxel_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(voxel_shader, "viewPos");

    // Ambient light level (some basic lighting)
    int ambientLoc = GetShaderLocation(voxel_shader, "ambient");
    SetShaderValue(voxel_shader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Shadow map resolution
    auto res = SHADOWMAP_RESOLUTION;
    SetShaderValue(voxel_shader, GetShaderLocation(voxel_shader, "shadowMapResolution"), &res, SHADER_UNIFORM_INT);

    // Create lights
    lights = std::vector<Light>();
    auto sun_pos = Vector3Scale(Vector3{32.0, 8.0, 32.0}, voxel_scale);
    auto sun_tgt = Vector3Scale(Vector3{48.0, 0.0, 48.0}, voxel_scale);
    camera_light_id = Light::create(DIRECTIONAL_LIGHT, camera.position, camera.target, WHITE, voxel_shader);
    sun_light_id = Light::create(DIRECTIONAL_LIGHT, sun_pos, sun_tgt, WHITE, voxel_shader);

    lights[sun_light_id].enabled = true;

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
    // Light Controls
    if (IsKeyReleased(KEY_Y)) move_camera_light = !move_camera_light;
    if (IsKeyReleased(KEY_U)) lights[sun_light_id].enabled = !lights[sun_light_id].enabled;
    if (IsKeyReleased(KEY_I)) lights[camera_light_id].enabled = !lights[camera_light_id].enabled;

    // Camera Light
    if (move_camera_light) {
        lights[camera_light_id].position = camera.position;
        lights[camera_light_id].target = camera.target;
    }

    if (IsKeyPressed(KEY_O)) lights[camera_light_id].light_camera.fovy += 1.0f;
    if (IsKeyPressed(KEY_P)) lights[camera_light_id].light_camera.fovy -= 1.0f;

    // Update
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

    Matrix light_view = {};
    Matrix light_proj = {};

    // PASS 1: Render all objects into the shadow map render texture
    for (Light& light : lights) {
        // if (!light.shadow_map || !light.enabled) continue;

        BeginTextureMode(*light.shadow_map);
        {
            // std::cout << light.id << ' ' << light.shadow_map->id << std::endl;

            ClearBackground(WHITE);

            if (light.enabled) {
                BeginMode3D(light.light_camera);
                {
                    light_view = rlGetMatrixModelview();
                    light_proj = rlGetMatrixProjection();
                    drawVoxelScene();
                }
                EndMode3D();
            }
        }
        EndTextureMode();

        // Update
        light.light_view_proj = MatrixMultiply(light_view, light_proj);
    }

    // PASS 2: Drawing
    BeginDrawing();
    {
        ClearBackground(RAYWHITE);

        rlEnableShader(voxel_shader.id);

        for (Light& light : lights) {
            rlActiveTextureSlot(light.texture_loc);
            rlEnableTexture(light.shadow_map->depth.id);
            rlSetUniform(light.shadow_map_loc, &light.texture_loc, SHADER_UNIFORM_INT, 1);
            SetShaderValueMatrix(voxel_shader, light.vp_loc, light.light_view_proj);

            // TraceLog(LOG_INFO, "light %d: unit=%d samplerLoc=%d fbo=%u depthTex=%u",
            //     light.id, light.texture_loc, light.shadow_map_loc,
            //     light.shadow_map->id, light.shadow_map->depth.id);
        }

        BeginMode3D(camera);
        {
            drawVoxelScene();

            // Shader Mode is only necessary for immediate draw calls
            BeginShaderMode(voxel_shader);
            {
                // Test Cube
                DrawCube(Vector3{0.0, 0.0, 0.0}, 1.0, 1.0, 1.0, ORANGE);
            }
            EndShaderMode();

            // Draw spheres to show where the lights are
            for (Light& light : lights) {
                if (light.enabled) DrawSphereEx(light.position, 0.2f, 8, 8, light.color);
                else DrawSphereWires(light.position, 0.2f, 8, 8, ColorAlpha(light.color, 0.3f));
            }
        }
        EndMode3D();

        // DrawTextureRec(lights[camera_light_id].shadow_map->depth,
        //             Rectangle{0, 0, float(SHADOWMAP_RESOLUTION), -float(SHADOWMAP_RESOLUTION)}, (Vector2){10, 10}, RED);
    }
    EndDrawing();
}

size_t Light::create(LightType type, Vector3 pos, Vector3 target, Color color, const Shader& shader) {
    // Emplace a default Light in the vector and fill it in-place.
    Light& light = global::lights.emplace_back();

    light.enabled = true;
    light.type = type == DIRECTIONAL_LIGHT ? 0 : 1;
    light.position = pos;
    light.target = target;
    light.color = color;

    light.id = global::next_light_id++;
    // NOTE: uniform names must match your shader
    light.enabled_loc  = GetShaderLocation(shader, TextFormat("lights[%i].enabled",  light.id));
    light.type_loc     = GetShaderLocation(shader, TextFormat("lights[%i].type",     light.id));
    light.position_loc = GetShaderLocation(shader, TextFormat("lights[%i].position", light.id));
    light.target_loc   = GetShaderLocation(shader, TextFormat("lights[%i].target",   light.id));
    light.color_loc    = GetShaderLocation(shader, TextFormat("lights[%i].color",    light.id));
    // If you actually use attenuation in the shader, set it too:
    // L.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", L.id));
    light.texture_loc = light.id + 10; // the 10 is kinda arbitrary
    light.vp_loc = GetShaderLocation(shader, TextFormat("lightVP%i", light.id));
    light.shadow_map_loc = GetShaderLocation(shader, TextFormat("shadowMap%i", light.id));

    // Light Camera for the shadow mapping algorithm
    light.light_camera = {
        light.position,
        light.target,
        { 0.0f, 1.0f, 0.0f },
        32.0f,
        CAMERA_ORTHOGRAPHIC
    };

    // Shadow Map
    light.shadow_map = new raylib::RenderTexture2D();
    auto fbo = rlLoadFramebuffer(); // load an empty framebuffer
    light.shadow_map->id = fbo;
    light.shadow_map->texture.width = SHADOWMAP_RESOLUTION;
    light.shadow_map->texture.height = SHADOWMAP_RESOLUTION;
    if (fbo > 0) {
        rlEnableFramebuffer(fbo);

        // Create depth texture
        light.shadow_map->depth.id = rlLoadTextureDepth(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION, false);
        light.shadow_map->depth.width = SHADOWMAP_RESOLUTION;
        light.shadow_map->depth.height = SHADOWMAP_RESOLUTION;
        // light.shadow_map->depth.format = PIXELFORMAT_COMPRESSED_ETC2_RGB; // Already written by rlLoadTextureDepth
        light.shadow_map->depth.mipmaps = 1;

        // Attach depth texture to framebuffer
        rlFramebufferAttach(fbo, light.shadow_map->depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        // Check if framebuffer is complete with attachments
        if (rlFramebufferComplete(fbo) > 0)
            TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", fbo);
        else
            TRACELOG(LOG_WARNING, "FBO: [ID %i] Framebuffer object created unsuccessfully", fbo);

        rlDisableFramebuffer();
    }
    else TRACELOG(LOG_WARNING, "FBO: Shadowmap frambuffer object can not be created!");

    TraceLog(LOG_DEBUG, "[Light] %zu: unit=%d locSamp=%d fbo=%u depthTex=%u pos=(%.2f,%.2f,%.2f) tgt=(%.2f,%.2f,%.2f)",
        light.id, light.texture_loc, light.shadow_map_loc,
        light.shadow_map->id, light.shadow_map->depth.id,
        light.position.x, light.position.y, light.position.z,
        light.target.x, light.target.y, light.target.z);

    return global::lights.size() - 1;
}

void Light::update(Shader shader) {
    // Move light camera
    light_camera.position = position;
    light_camera.target = target;

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

Light::~Light() {
    if (shadow_map) {
        // Only unload if it looks valid
        if (shadow_map->id != 0) {
            UnloadRenderTexture(*shadow_map);
        }
        delete shadow_map;
        shadow_map = nullptr;
    }
    else { TraceLog(LOG_DEBUG, "[Light] %i: shadow map already freed!", id); }
}

// Move constructor
Light::Light(Light&& other) noexcept
    : id(other.id)
    , type(other.type)
    , enabled(other.enabled)
    , position(other.position)
    , target(other.target)
    , color(other.color)
    , attenuation(other.attenuation)
    , light_camera(other.light_camera)
    , shadow_map(other.shadow_map)                   // take ownership
    , light_view_proj(other.light_view_proj)
    , enabled_loc(other.enabled_loc)
    , type_loc(other.type_loc)
    , position_loc(other.position_loc)
    , target_loc(other.target_loc)
    , color_loc(other.color_loc)
    , attenuation_loc(other.attenuation_loc)
    , vp_loc(other.vp_loc)
    , shadow_map_loc(other.shadow_map_loc)
    , texture_loc(other.texture_loc)
{
    // leave 'other' in a destructible state
    other.shadow_map = nullptr;
}

// Move assignment
Light& Light::operator=(Light&& other) noexcept {
    if (this != &other) {
        // Release current ownership first
        if (shadow_map) {
            if (shadow_map->id != 0) {
                UnloadRenderTexture(*shadow_map);
            }
            delete shadow_map;
        }

        // Trivially copy POD/aggregate members
        id           = other.id;
        type         = other.type;
        enabled      = other.enabled;
        position     = other.position;
        target       = other.target;
        color        = other.color;
        attenuation  = other.attenuation;

        light_camera    = other.light_camera;
        light_view_proj = other.light_view_proj;

        // Take ownership of the render texture pointer
        shadow_map   = other.shadow_map;
        other.shadow_map = nullptr;

        // Shader locations (just copy)
        enabled_loc     = other.enabled_loc;
        type_loc        = other.type_loc;
        position_loc    = other.position_loc;
        target_loc      = other.target_loc;
        color_loc       = other.color_loc;
        attenuation_loc = other.attenuation_loc;
        vp_loc          = other.vp_loc;
        shadow_map_loc  = other.shadow_map_loc;
        texture_loc     = other.texture_loc;
    }
    return *this;
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

std::string global::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

raylib::Shader global::loadAndPatchShader(const std::string& shader_path, int light_count) {
    std::string vertex = loadFile(shader_path + ".vs");
    std::string fragment = loadFile(shader_path + ".fs");

    // Regex for finding the declaration in the file
    static const std::regex shadow_decl{
        R"(\buniform\s+sampler2D\s+shadowMap\b\s*;)",
        std::regex::ECMAScript
    };
    static const std::regex vp_decl{
        R"(\buniform\s+mat4\s+lightVP\b\s*;)",
        std::regex::ECMAScript
    };
    static const std::regex shadow_get_decl{R"(GetShadowMapFunction)", std::regex::ECMAScript};
    static const std::regex vp_get_decl{R"(GetLightVPFunction)", std::regex::ECMAScript};
    static const std::regex max_lights_define{R"(#define MAX_LIGHTS x)", std::regex::ECMAScript};

    // Build replacement block
    std::ostringstream shadow_oss, vp_oss, shadow_get_oss, vp_get_oss;
    for (std::size_t i = 0; i < light_count; ++i) {
        shadow_oss << "uniform sampler2D shadowMap" << i << ";\n";
        vp_oss << "uniform mat4 lightVP" << i << ";\n";

        if (i != light_count - 1) {
            shadow_get_oss << "    if (i == " << i << ") return texture(shadowMap" << i << ", uv).r;\n";
            vp_get_oss     << "    if (i == " << i << ") return lightVP"   << i << ";\n";
        } else {
            // last iterator
            shadow_get_oss << "    return texture(shadowMap" << i << ", uv).r;";
            vp_get_oss     << "    return lightVP"   << i << ";";
        }
    }
    // Patching
    auto fragment_patched = std::regex_replace(fragment, shadow_decl, shadow_oss.str());
    fragment_patched = std::regex_replace(fragment_patched, vp_decl, vp_oss.str());
    fragment_patched = std::regex_replace(fragment_patched, shadow_get_decl, shadow_get_oss.str());
    fragment_patched = std::regex_replace(fragment_patched, vp_get_decl, vp_get_oss.str());
    std::string new_lights_define = "#define MAX_LIGHTS " + std::to_string(light_count);
    fragment_patched = std::regex_replace(fragment_patched, max_lights_define, new_lights_define);

    return LoadShaderFromMemory(vertex.c_str(), fragment_patched.c_str());
}

void global::drawVoxelScene() {
    for (VoxelGrid* grid : voxel_grids) {
        for (ModelInfo* model_info : grid->get_models()) {
            drawVoxelModel(*model_info);
        }
    }
}

void global::drawVoxelModel(const ModelInfo& model_info) {
    // Offset
    auto offset = Vector3Scale(model_info.transform.translation, voxel_scale);

    // Rotation
    auto axis = Vector3{};
    auto angle = 0.0f;
    QuaternionToAxisAngle(model_info.transform.rotation, &axis, &angle);

    // Scale
    auto scale = Vector3Scale(model_info.transform.scale, voxel_scale);

    // Drawing the model
    DrawModelEx(model_info.model, offset,
        axis, angle, scale, WHITE);

    // Drawing wires
    // DrawModelWiresEx(model_info->model, offset,
    //     axis, angle, scale, DARKGRAY);
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