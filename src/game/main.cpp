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

    root_view = std::make_unique<ViewNode>(nullptr);

    voxel_shader = loadAndPatchShader("../resources/shaders/lighting", 2);
    voxel_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(voxel_shader, "viewPos");

    // Ambient light level (some basic lighting)
    int ambientLoc = GetShaderLocation(voxel_shader, "ambient");
    SetShaderValue(voxel_shader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Shadow map resolution
    auto res = SHADOWMAP_RESOLUTION;
    SetShaderValue(voxel_shader, GetShaderLocation(voxel_shader, "shadowMapResolution"), &res, SHADER_UNIFORM_INT);

    // Voxels
    root_view->add_child(std::make_unique<VoxelView>(root_view.get(), &voxel_shader));

    // voxel_editor = VoxelEditor();
    TraceLog(LOG_DEBUG, "main init finished!");
}

void global::shutdown() {
    // TODO: this function should be called, but it produces a double free
    //  figure out how to call it without the error
    // UnloadShader(shader);

    raylib::Window::Close();
}

void global::mainLoop() {
    TraceLog(LOG_DEBUG, ".main loop");
    // voxel_editor.update(); move to ViewNode tree
    root_view->update(GetFrameTime());
    root_view->render();
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

Transform transform_transform(const Transform &base, const Transform &applied) {
    Transform result;

    result.scale.x = base.scale.x * applied.scale.x;
    result.scale.y = base.scale.y * applied.scale.y;
    result.scale.z = base.scale.z * applied.scale.z;

    result.rotation = QuaternionMultiply(applied.rotation, base.rotation);

    Vector3 scaled = Vector3Multiply(base.translation, applied.scale);
    Vector3 rotated = Vector3RotateByQuaternion(scaled, applied.rotation);
    result.translation = Vector3Add(rotated, applied.translation);

    return result;
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
