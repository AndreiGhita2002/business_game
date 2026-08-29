//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"

#include <cfloat>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <regex>

#include "raylib-cpp.hpp"
#include "voxel/VoxelMesher.hpp"
#include "voxel/SingleChunkGrid.hpp"
#include "ui/UIView.hpp"
#include "ui/UILabel.hpp"
#include "ui/UIButton.hpp"
#include "ui/UIImage.hpp"
#include "ui/ShaderMenu.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

#define GLSL_VERSION 330

// Layout of the debug buttons, in pixels
constexpr float UI_MARGIN = 16.0f;
constexpr float UI_BUTTON_HEIGHT = 32.0f;
constexpr float UI_BUTTON_GAP = 8.0f;

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
    auto voxel_view = static_cast<VoxelView *>(root_view->child.get());

    // UI
    // Added after the VoxelView, so it ends up as its sibling and is rendered
    // once the voxel scene is already on screen.
    auto ui_view_node = std::make_unique<UIView>(root_view.get());
    auto ui_view = ui_view_node.get();
    root_view->add_child(std::move(ui_view_node));

    // Title, pinned to the top of the window. It carries a background, as the
    // sky behind it is nearly the same colour as the text.
    auto title = std::make_unique<UILabel>(ui_view, "business game",
        Rectangle{0.0f, 12.0f, 0.0f, 0.0f}, Anchor::TOP_CENTER);
    title->font_size = 32.0f;
    title->background = ui_view->style.background;
    ui_view->add_child(std::move(title));

    // Debug panel for the lighting, hidden until F3 or until its button is
    // pressed. It sits under that button, in the top left.
    auto shader_menu_node = std::make_unique<ShaderMenu>(ui_view, &voxel_shader);
    auto shader_menu = shader_menu_node.get();
    shader_menu->bounds = Rectangle{UI_MARGIN, UI_MARGIN + UI_BUTTON_HEIGHT + UI_BUTTON_GAP, 0.0f, 0.0f};

    // The light boxes are not shader uniforms, but they belong on the same
    // panel. These two replace the O and P keys, which only reached the camera
    // light. Pointers into the light vector are stable because it is reserved
    // at its final size and never grown again.
    shader_menu->add_value_row("sun box",
        &voxel_view->lights[voxel_view->sun_light_id].light_camera.fovy,
        8.0f, 8.0f, 512.0f, 0, {});
    shader_menu->add_value_row("camera box",
        &voxel_view->lights[voxel_view->camera_light_id].light_camera.fovy,
        8.0f, 8.0f, 512.0f, 0, {});

    ui_view->add_child(std::move(shader_menu_node));

    ui_view->add_child(std::make_unique<UIButton>(ui_view, "Shader Menu",
        [shader_menu] { shader_menu->visible = !shader_menu->visible; },
        Rectangle{UI_MARGIN, UI_MARGIN, 0.0f, UI_BUTTON_HEIGHT}, Anchor::TOP_LEFT));

    // A button for everything that used to be on a key only. The keys still
    // work: see VoxelView::updateLights. Buttons stack upwards from the bottom
    // left corner, so the first one added ends up lowest.
    float button_y = UI_MARGIN;
    auto add_bottom_left_button = [&](const char* text, std::function<void()> action) {
        ui_view->add_child(std::make_unique<UIButton>(ui_view, text, std::move(action),
            Rectangle{UI_MARGIN, button_y, 0.0f, UI_BUTTON_HEIGHT}, Anchor::BOTTOM_LEFT));
        button_y += UI_BUTTON_HEIGHT + UI_BUTTON_GAP;
    };

    // U
    add_bottom_left_button("Toggle Sun", [voxel_view] {
        Light& sun = voxel_view->lights[voxel_view->sun_light_id];
        sun.enabled = !sun.enabled;
    });
    // I
    add_bottom_left_button("Toggle Camera Light", [voxel_view] {
        Light& camera_light = voxel_view->lights[voxel_view->camera_light_id];
        camera_light.enabled = !camera_light.enabled;
    });
    // Y
    add_bottom_left_button("Light Follows Camera", [voxel_view] {
        voxel_view->move_camera_light = !voxel_view->move_camera_light;
    });

    // The voxel editor is a UI panel now, so it lives under the UIView. It is
    // added last, which puts it on top of the elements before it.
    ui_view->add_child(std::make_unique<VoxelEditor>(ui_view, voxel_view));

    TraceLog(LOG_DEBUG, "main init finished!");
}

void global::shutdown() {
    UnloadShader(voxel_shader);

    // The view tree is torn down before the window, so that anything it holds
    // on the GPU (UI textures, meshes) is released while the context is alive.
    root_view.reset();

    raylib::Window::Close();
}

void global::mainLoop() {
    root_view->update(GetFrameTime());

    // The whole frame is drawn inside a single Begin/EndDrawing block, so that
    // every ViewNode draws in tree order: the voxel scene first, the UI on top.
    BeginDrawing(); {
        ClearBackground(RAYWHITE);
        root_view->render();
    }
    EndDrawing();
}

Vector3 apply_transform_trans(const Vector3 v, const Transform &t) {
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

Quaternion apply_transform_rot(Quaternion rot, const Transform &t) {
    return QuaternionAdd(rot, t.rotation);
}

Vector3 apply_transform_scale(Vector3 scale, const Transform &t) {
    return Vector3{
        scale.x * t.scale.x,
        scale.y * t.scale.y,
        scale.z * t.scale.z
    };
}

void apply_transform(Vector3* position, Quaternion* rotation, Vector3* scale, const Transform& t) {
    *position = apply_transform_trans(*position, t);
    *rotation = apply_transform_rot(*rotation, t);
    *scale = apply_transform_scale(*scale, t);
}

Matrix voxel_model_matrix(const VoxelGrid* grid, const ModelInfo& model_info) {
    // Two steps, in the order a scene graph applies them: the model sits
    // somewhere inside its grid, and the grid sits somewhere in the world,
    // which is its own transform with every parent's folded in.
    const Transform world = transform_transform(model_info.transform, grid->get_world_transform());

    // transform_to_matrix uses the same order as DrawModelEx - scale, rotate,
    // translate - and the model's own matrix goes on top of it
    return MatrixMultiply(model_info.model.transform, transform_to_matrix(world));
}

bool find_voxel_on_ray(const Ray ray, const std::vector<VoxelGrid*>* voxel_grids,
                       const char* grid_type, VoxelRayHit* out) {
    VoxelRayHit best{};
    best.collision.distance = FLT_MAX;
    bool found = false;

    for (VoxelGrid* grid : *voxel_grids) {
        // Filter by grid type if specified
        if (grid_type != nullptr && grid->get_grid_type().compare(grid_type) != 0)
            continue;

        for (ModelInfo* model_info : grid->get_models()) {
            // SingleChunkGrid reports a null model until it has been meshed
            if (model_info == nullptr || !model_info->do_render) continue;

            const Matrix world_mat = voxel_model_matrix(grid, *model_info);

            for (int i = 0; i < model_info->model.meshCount; ++i) {
                RayCollision c = GetRayCollisionMesh(ray, model_info->model.meshes[i], world_mat);

                if (c.hit && c.distance < best.collision.distance) {
                    best = VoxelRayHit{grid, model_info, c, world_mat};
                    found = true;
                }
            }
        }
    }

    if (found && out != nullptr) *out = best;
    return found;
}

/* Finds the closest VoxelGrid intersected by a ray.
 *  Parameters:
 *   ray         - The ray to test against (in world space)
 *   voxel_grids - Collection of grids to test
 *   grid_type   - Filter by grid type name (e.g. "SingleChunkGrid"), or nullptr to test all grids
 * Returns the closest intersected grid, or nullptr if no intersection found.
 */
VoxelGrid* find_grid_on_ray(Ray ray, const std::vector<VoxelGrid*>* voxel_grids, const char* grid_type) {
    VoxelRayHit hit{};
    if (!find_voxel_on_ray(ray, voxel_grids, grid_type, &hit)) return nullptr;
    return hit.grid;
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

Transform transform_relative_to(const Transform &world, const Transform &parent_world) {
    Transform local;

    // Each line undoes the matching one in transform_transform, in reverse
    local.scale = Vector3{
        parent_world.scale.x != 0.0f ? world.scale.x / parent_world.scale.x : 0.0f,
        parent_world.scale.y != 0.0f ? world.scale.y / parent_world.scale.y : 0.0f,
        parent_world.scale.z != 0.0f ? world.scale.z / parent_world.scale.z : 0.0f,
    };

    const Quaternion parent_inverse = QuaternionInvert(parent_world.rotation);
    local.rotation = QuaternionMultiply(parent_inverse, world.rotation);

    // Translate back, then turn back, then scale back: the reverse of the
    // order transform_transform applies them in
    const Vector3 moved = Vector3Subtract(world.translation, parent_world.translation);
    const Vector3 turned = Vector3RotateByQuaternion(moved, parent_inverse);
    local.translation = Vector3{
        parent_world.scale.x != 0.0f ? turned.x / parent_world.scale.x : 0.0f,
        parent_world.scale.y != 0.0f ? turned.y / parent_world.scale.y : 0.0f,
        parent_world.scale.z != 0.0f ? turned.z / parent_world.scale.z : 0.0f,
    };

    return local;
}

Matrix transform_to_matrix(Transform t) {
    Matrix scale = MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    Matrix rotation = QuaternionToMatrix(t.rotation);
    Matrix translation = MatrixTranslate(t.translation.x, t.translation.y, t.translation.z);
    // Order: scale, then rotate, then translate
    return MatrixMultiply(MatrixMultiply(scale, rotation), translation);
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

void print_matrix(const Matrix& mat) {
    std::cout << "[\n";
    std::cout << "  " << mat.m0  << ", " << mat.m4  << ", " << mat.m8  << ", " << mat.m12 << "\n";
    std::cout << "  " << mat.m1  << ", " << mat.m5  << ", " << mat.m9  << ", " << mat.m13 << "\n";
    std::cout << "  " << mat.m2  << ", " << mat.m6  << ", " << mat.m10 << ", " << mat.m14 << "\n";
    std::cout << "  " << mat.m3  << ", " << mat.m7  << ", " << mat.m11 << ", " << mat.m15 << "\n";
    std::cout << "]\n";
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
