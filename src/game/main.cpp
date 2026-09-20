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
#include "ui/GridTransformMenu.hpp"

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

    // Escape is a tool's "give up on this selection" key - the attachment menu
    // and the grid transform menu both offer it - so it cannot also be the one
    // that closes the window. The window button is the way out now.
    SetExitKey(KEY_NULL);

    root_view = std::make_unique<ViewNode>(nullptr);

    voxel_shader = loadAndPatchShader("../resources/shaders/lighting", 2);
    voxel_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(voxel_shader, "viewPos");

    // Ambient light level (some basic lighting)
    int ambientLoc = GetShaderLocation(voxel_shader, "ambient");
    SetShaderValue(voxel_shader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

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

    // Where the sun sits in the sky. These are not shader uniforms, but they
    // belong on the same panel: Light::update() sends the direction it works
    // out from them every frame, so the rows need no callback of their own.
    // Pointers into the light vector are stable because it is reserved at its
    // final size and never grown again.
    // A very low sun is left out of the range on purpose - the flatter the
    // angle, the further a shadow ray has to travel before it clears the
    // terrain, and the sooner it runs into the shader's step cap.
    shader_menu->add_value_row("sun elevation",
        &voxel_view->lights[voxel_view->sun_light_id].elevation,
        1.0f, 5.0f, 90.0f, 0, {});
    shader_menu->add_value_row("sun azimuth",
        &voxel_view->lights[voxel_view->sun_light_id].azimuth,
        5.0f, 0.0f, 360.0f, 0, {});

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

    // Moving and turning a grid, on the right edge. Its rows and its attachment
    // buttons only appear once a grid has been picked out of the world: the
    // grid selected there is the one that gets moved, and the one that gets
    // attached to something else.
    auto transform_menu_node = std::make_unique<GridTransformMenu>(ui_view, voxel_view);
    auto transform_menu = transform_menu_node.get();
    ui_view->add_child(std::move(transform_menu_node));

    // The voxel editor is a UI panel now, so it lives under the UIView. It is
    // added last, which puts it on top of the elements before it.
    auto editor_node = std::make_unique<VoxelEditor>(ui_view, voxel_view);
    auto editor = editor_node.get();
    ui_view->add_child(std::move(editor_node));

    // Both act on a click in the world, so only one of them may be armed at a
    // time: otherwise a single click would be read as a voxel to place and as a
    // grid to pick at once. Each switches the other off as it is armed, which
    // is what these hooks are for - neither knows the other exists, and this is
    // the one place they meet.
    transform_menu->on_activate = [editor] {
        editor->select(NO_VOXEL_SELECTION);
    };
    editor->on_select = [transform_menu] {
        transform_menu->cancel();
    };

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

    // Only the light count is patched in. The shadow map samplers and the light
    // matrices that used to be unrolled here went with the shadow pass: the
    // shader traces its shadows through the world volume now, which is one
    // texture however many lights there are.
    static const std::regex max_lights_define{R"(#define MAX_LIGHTS x)", std::regex::ECMAScript};
    const std::string new_lights_define = "#define MAX_LIGHTS " + std::to_string(light_count);
    const std::string fragment_patched = std::regex_replace(fragment, max_lights_define, new_lights_define);

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
