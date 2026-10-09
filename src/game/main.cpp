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
#include "ui/VehiclePanel.hpp"

#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>
#endif

#define GLSL_VERSION 330

// Layout of the debug buttons, in pixels
constexpr float UI_MARGIN = 16.0f;
constexpr float UI_BUTTON_HEIGHT = 32.0f;
constexpr float UI_BUTTON_GAP = 8.0f;

// The most ticks one frame will run. A frame that falls further behind than
// this (a breakpoint, the window being dragged) drops the time instead of
// trying to catch up, which would only make the next frame later still.
constexpr int MAX_TICKS_PER_FRAME = 5;

// The simulation's seed. Nothing random happens in it yet.
constexpr uint64_t SIMULATION_SEED = 1;

// Where F5 saves the game and F9 loads it from. Relative to build/, the same
// way the shaders are, so it lands in the repository's saves/ folder (ignored
// by git) rather than in build/, which `make clean` would take with it.
//
// TODO (ui): a save menu - named saves, a list to load from, and a warning
//  before loading over a game that has not been saved. One quicksave file on
//  F5 and F9 stands in until then.
constexpr const char* QUICKSAVE_PATH = "../saves/quicksave.bgsave";
constexpr int QUICKSAVE_KEY = KEY_F5;
constexpr int QUICKLOAD_KEY = KEY_F9;

// How long a status line stays in the readout, in seconds
constexpr float STATUS_SECONDS = 3.0f;

/** F5 saves the game, F9 loads it. */
static void handle_save_keys();

/**
 * Queues the test scenario for the architecture slice: two loops of road over
 * the map and a dozen cars on them at different speeds, two running their loop
 * backwards. Goes through the command queue like anything else would, so the
 * setup runs at tick 0 and the cars spawn at tick 1, once the routes exist.
 *
 * The routes follow the terrain: every point takes its height from the top of
 * the map's column there. That reads the voxels, which the simulation must
 * never do, but this is not the simulation - it is input, worked out before
 * the command is made, and the command only carries plain numbers.
 */
static void queue_test_scenario(VoxelMap* map);

void global::init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);  // Enable Multi Sampling Anti Aliasing 4x (if available)
    raylib::Window::Init(1600, 900, "business game");

    // Escape is a tool's "give up on this selection" key - the attachment menu
    // and the grid transform menu both offer it - so it cannot also be the one
    // that closes the window. The window button is the way out now.
    SetExitKey(KEY_NULL);

    root_view = std::make_unique<ViewNode>(nullptr);
    simulation = std::make_unique<sim::Simulation>(SIMULATION_SEED);

    voxel_shader = loadAndPatchShader("../resources/shaders/lighting", 2, MAX_GRID_VOLUMES);
    voxel_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(voxel_shader, "viewPos");

    // Ambient light level (some basic lighting)
    int ambientLoc = GetShaderLocation(voxel_shader, "ambient");
    SetShaderValue(voxel_shader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Voxels
    root_view->add_child(std::make_unique<VoxelView>(root_view.get(), &voxel_shader));
    voxel_view = static_cast<VoxelView *>(root_view->child.get());

    // The simulation's vehicles, made visible. Every asset is put on the map's
    // colours, and the placeholder cars come in three of them.
    assets = std::make_unique<AssetRegistry>(voxel_view->game_map->voxel_colours);
    assets->register_builder("car.maroon", placeholder_car_builder(10), PLACEHOLDER_CAR_PIVOT);
    assets->register_builder("car.blue", placeholder_car_builder(4), PLACEHOLDER_CAR_PIVOT);
    assets->register_builder("car.orange", placeholder_car_builder(5), PLACEHOLDER_CAR_PIVOT);
    entities = std::make_unique<EntityManager>(voxel_view, assets.get(), voxel_view);
    queue_test_scenario(voxel_view->game_map);

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

    // What the simulation is doing, under the title: the tick, how many ticks
    // the last frame ran, and how many vehicles exist against how many are
    // drawn, which is the realise radius at work
    auto sim_readout_node = std::make_unique<UILabel>(ui_view, "",
        Rectangle{0.0f, 54.0f, 0.0f, 0.0f}, Anchor::TOP_CENTER);
    auto sim_readout = sim_readout_node.get();
    sim_readout->font_size = 16.0f;
    sim_readout->background = ui_view->style.background;
    ui_view->add_child(std::move(sim_readout_node));
    add_script(std::make_unique<LambdaScript>("simulation readout", [sim_readout](const float delta) {
        sim_readout->text = TextFormat("tick %llu  |  %d ticks this frame  |  %zu vehicles, %zu drawn",
            static_cast<unsigned long long>(simulation->tick()), ticks_last_frame,
            simulation->vehicles().size(), entities->realized_count());
        if (status_seconds_left > 0.0f) {
            sim_readout->text += "  |  " + status_message;
            status_seconds_left -= delta;
        }
    }));

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
    add_bottom_left_button("Toggle Sun", [] {
        Light& sun = voxel_view->lights[voxel_view->sun_light_id];
        sun.enabled = !sun.enabled;
    });

    // The selected vehicle, under the readout. Added before the menus below so
    // that it is updated before them: a click that arms one of them is then
    // seen by this panel while that tool still counts as idle, and the panel
    // checks world_click_taken on every later click.
    auto vehicle_panel_node = std::make_unique<VehiclePanel>(ui_view, voxel_view, entities.get(),
                                                             simulation.get(), &commands);
    auto vehicle_panel = vehicle_panel_node.get();
    ui_view->add_child(std::move(vehicle_panel_node));

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
    // A test script: spin the small grid about its up axis. The grid is the
    // second one the VoxelView made, after the map. The VoxelView owns it, and
    // the scripts are cleared before the view tree in shutdown(), so the
    // pointer never outlives it.
    VoxelGrid* small_grid = voxel_view->voxel_grids[1];
    add_script(std::make_unique<LambdaScript>("spin small grid", [small_grid](float delta) {
        constexpr float TURNS_PER_SECOND = 0.25f;
        // Model space Y is up (grid z). Normalised every frame so the rounding
        // in a long run of multiplications cannot drift it off a unit quaternion.
        const Quaternion step = QuaternionFromAxisAngle(
            Vector3{0.0f, 1.0f, 0.0f}, TURNS_PER_SECOND * 2.0f * PI * delta);
        Transform t = small_grid->get_transform();
        t.rotation = QuaternionNormalize(QuaternionMultiply(step, t.rotation));
        small_grid->set_transform(t);
    }));

    transform_menu->on_activate = [editor] {
        editor->select(NO_VOXEL_SELECTION);
    };
    editor->on_select = [transform_menu] {
        transform_menu->cancel();
    };

    // A free click on a car selects it, but only when neither of those two is
    // waiting on the click for itself
    vehicle_panel->world_click_taken = [transform_menu, editor] {
        return transform_menu->is_active() || editor->is_active();
    };

    // An entity's grids are deleted when it leaves the camera's range, and
    // these all hold grid pointers they may still be using
    voxel_view->add_grid_removal_listener([transform_menu, editor](const std::vector<VoxelGrid*>& removed) {
        transform_menu->forget_grids(removed);
        editor->forget_grids(removed);
    });

    TraceLog(LOG_DEBUG, "main init finished!");
}

void global::shutdown() {
    // The shader is freed here, while the window and its GL context are still
    // up, and then emptied out by hand.
    //
    // Both halves matter. UnloadShader() takes the shader by value, so it frees
    // shader.locs without the copy out here ever hearing about it, and
    // raylib::Shader's destructor unloads again at exit on the strength of
    // locs still not being null - after the window is gone, and on a pointer
    // that was freed the first time round. That was the double free on
    // shutdown. raylib::Shader::Unload() is no use either: it tests the same
    // stale locs and leaves it just as stale.
    // Scripts go first, as they may hold pointers into the view tree.
    scripts.clear();

    // Then the entities, while the view they lent their grids to is still
    // there to take them back. Its destructor deletes whatever is left in it,
    // which must by then be only its own grids.
    entities.reset();

    UnloadShader(voxel_shader);
    voxel_shader.locs = nullptr;
    voxel_shader.id = 0;

    // The view tree is torn down before the window, so that anything it holds
    // on the GPU (UI textures, meshes) is released while the context is alive.
    root_view.reset();
    voxel_view = nullptr;

    assets.reset();
    simulation.reset();

    raylib::Window::Close();
}

void global::mainLoop() {
    const float delta = GetFrameTime();
    const float tick_length = tick_seconds();

    // Between ticks, before any run this frame, so a save never catches the
    // simulation part way through one and a load starts on a clean frame
    handle_save_keys();

    // Fixed ticks out of a frame of whatever length. The accumulator holds the
    // game time not run yet; every whole tick of it is one step.
    tick_accumulator += delta * game_speed;
    int ticks_run = 0;
    while (tick_accumulator >= tick_length && ticks_run < MAX_TICKS_PER_FRAME) {
        simulation->step(commands.take(simulation->tick()));
        entities->on_tick(*simulation);
        tick_accumulator -= tick_length;
        ticks_run++;
    }
    if (ticks_run == MAX_TICKS_PER_FRAME) tick_accumulator = 0.0f;
    ticks_last_frame = ticks_run;

    // The entities are drawn the leftover fraction of the way between the
    // last two ticks, which is what makes 20 ticks a second look smooth
    entities->present(*simulation, voxel_view->camera.position,
                      tick_accumulator / tick_length, delta);

    for (const auto& script : scripts) {
        script->on_update(delta);
    }
    root_view->update(delta);

    // The whole frame is drawn inside a single Begin/EndDrawing block, so that
    // every ViewNode draws in tree order: the voxel scene first, the UI on top.
    BeginDrawing(); {
        ClearBackground(RAYWHITE);
        root_view->render();
    }
    EndDrawing();
}

static void queue_test_scenario(VoxelMap* map) {
    // Outside the namespace, but it is setting up global state throughout
    using namespace global;
    using sim::Fixed;
    using sim::Point;

    // One above the highest solid voxel in the column, so the cars stand on
    // the ground rather than in it
    const auto ground = [map](const int x, const int y) {
        for (int z = CHUNK_SIZE - 1; z >= 0; --z) {
            if (map->is_solid(Int3{x, y, z})) return z + 1;
        }
        return 0;
    };

    // A rectangle through (x0, y0) and (x1, y1), with a point every `step`
    // voxels along each side. Each point is on the ground, and a car takes its
    // height from the two points either side of it, so the closer they are
    // the less it cuts through the bumps in between.
    const auto loop = [&ground](const int x0, const int y0, const int x1, const int y1, const int step) {
        std::vector<Point> points;
        const auto add = [&](const int x, const int y) {
            points.push_back(Point{Fixed::from_int(x), Fixed::from_int(y), Fixed::from_int(ground(x, y))});
        };
        for (int x = x0; x < x1; x += step) add(x, y0);
        for (int y = y0; y < y1; y += step) add(x1, y);
        for (int x = x1; x > x0; x -= step) add(x, y1);
        for (int y = y1; y > y0; y -= step) add(x0, y);
        return points;
    };

    commands.submit(std::make_unique<sim::AddRoute>(loop(12, 12, 116, 116, 2)));
    commands.submit(std::make_unique<sim::AddRoute>(loop(40, 40, 88, 72, 2)));

    // The routes do not exist until tick 0 has run, and the queue hands every
    // command to the next tick, so the cars are queued from a script that
    // waits for the routes to be announced. It removes nothing and does
    // nothing once it has fired.
    add_script(std::make_unique<LambdaScript>("spawn test cars", [fired = false](float) mutable {
        if (fired || simulation->routes().size() < 2) return;
        fired = true;

        // Route handles in the order they were added. Nothing else adds
        // routes, so walking the pool gives exactly these two.
        std::vector<sim::RouteId> routes;
        simulation->routes().for_each([&routes](const sim::RouteId id, const sim::Route&) {
            routes.push_back(id);
        });

        const char* colours[] = {"car.maroon", "car.blue", "car.orange"};
        // Outer loop: eight cars spread round it at 2 to 5.5 voxels a second
        const Fixed outer_length = simulation->routes().get(routes[0])->length();
        for (int i = 0; i < 8; ++i) {
            commands.submit(std::make_unique<sim::SpawnVehicle>(
                routes[0], outer_length * i / 8, Fixed::from_ratio(4 + i, 40), colours[i % 3]));
        }
        // Inner loop: four cars, the last two going round the other way
        const Fixed inner_length = simulation->routes().get(routes[1])->length();
        for (int i = 0; i < 4; ++i) {
            const Fixed speed = Fixed::from_ratio(3 + i, 40);
            commands.submit(std::make_unique<sim::SpawnVehicle>(
                routes[1], inner_length * i / 4, i < 2 ? speed : -speed, colours[(i + 1) % 3]));
        }
    }));
}

void global::show_status(const std::string& message) {
    status_message = message;
    status_seconds_left = STATUS_SECONDS;
}

static void handle_save_keys() {
    using namespace global;

    if (IsKeyPressed(QUICKSAVE_KEY)) {
        std::string error;
        if (sim::save_to_file(QUICKSAVE_PATH, *simulation, commands, &error)) {
            TraceLog(LOG_INFO, "SAVE: saved tick %llu to %s",
                     static_cast<unsigned long long>(simulation->tick()), QUICKSAVE_PATH);
            show_status(TextFormat("saved at tick %llu", static_cast<unsigned long long>(simulation->tick())));
        } else {
            TraceLog(LOG_WARNING, "SAVE: %s", error.c_str());
            show_status("save failed: " + error);
        }
    }

    if (IsKeyPressed(QUICKLOAD_KEY)) {
        std::string error;
        std::optional<sim::LoadedGame> loaded = sim::load_from_file(QUICKSAVE_PATH, &error);
        if (!loaded) {
            // The game carries on untouched: nothing is replaced until the
            // whole file has read cleanly
            TraceLog(LOG_WARNING, "LOAD: %s", error.c_str());
            show_status("load failed: " + error);
            return;
        }

        // Nothing on screen is saved. The entities are dropped and the next
        // present() builds the ones near the camera again from the loaded
        // simulation, as it would after any clear().
        entities->clear();
        // Assigned into the existing objects rather than replacing them, as
        // the vehicle panel holds pointers to both
        *simulation = std::move(loaded->simulation);
        commands = std::move(loaded->commands);
        tick_accumulator = 0.0f;

        TraceLog(LOG_INFO, "LOAD: loaded tick %llu from %s",
                 static_cast<unsigned long long>(simulation->tick()), QUICKSAVE_PATH);
        show_status(TextFormat("loaded tick %llu", static_cast<unsigned long long>(simulation->tick())));
    }
}

Script* global::add_script(std::unique_ptr<Script> script) {
    Script* added = script.get();
    scripts.push_back(std::move(script));
    added->on_start();
    return added;
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

raylib::Shader global::loadAndPatchShader(const std::string& shader_path, int light_count,
                                          int max_grid_volumes) {
    std::string vertex = loadFile(shader_path + ".vs");
    std::string fragment = loadFile(shader_path + ".fs");

    // Two array sizes are patched in, so that neither can drift away from the
    // constant the C++ side sizes its own arrays by. The shadow map samplers
    // and light matrices that used to be unrolled here went with the shadow
    // pass: shadows are traced through the voxel volumes now.
    static const std::regex max_lights_define{R"(#define MAX_LIGHTS x)", std::regex::ECMAScript};
    static const std::regex max_grid_volumes_define{R"(#define MAX_GRID_VOLUMES x)", std::regex::ECMAScript};

    const std::string new_lights_define = "#define MAX_LIGHTS " + std::to_string(light_count);
    const std::string new_grid_volumes_define =
        "#define MAX_GRID_VOLUMES " + std::to_string(max_grid_volumes);

    std::string fragment_patched = std::regex_replace(fragment, max_lights_define, new_lights_define);
    fragment_patched = std::regex_replace(fragment_patched, max_grid_volumes_define, new_grid_volumes_define);

    return LoadShaderFromMemory(vertex.c_str(), fragment_patched.c_str());
}

int main() {
    global::init();

#if defined(PLATFORM_WEB)
    // The function itself, not a call to it
    emscripten_set_main_loop(global::mainLoop, 0, 1);
#else
    SetTargetFPS(60);

    while (!raylib::Window::ShouldClose()) {
        global::mainLoop();
    }
#endif
    global::shutdown();
    return 0;
}
