//
// Created by Andrei Ghita on 01.09.2025.
//

#include "main.hpp"

#include <algorithm>
#include <cfloat>
#include <iostream>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <stdexcept>
#include <regex>

#include "raylib-cpp.hpp"
#include <rlgl.h>
#include "entity/TerrainVoxels.hpp"
#include "sim/Island.hpp"
#include "voxel/VoxelMesher.hpp"
#include "voxel/SingleChunkGrid.hpp"
#include "ui/UIView.hpp"
#include "ui/UILabel.hpp"
#include "ui/UIButton.hpp"
#include "ui/UIImage.hpp"
#include "ui/GameSettingsMenu.hpp"
#include "ui/ShaderMenu.hpp"
#include "ui/UINumberRow.hpp"
#include "ui/GridTransformMenu.hpp"
#include "ui/IslandMenu.hpp"
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

// The camera's near and far clip planes, in world units (a voxel is one), see
// init(). The far one covers the default world corner to corner.
constexpr double CAMERA_NEAR = 0.5;
constexpr double CAMERA_FAR = 4000.0;

// The least serious log message printed, see init()
constexpr TraceLogLevel LOG_LEVEL = LOG_WARNING;

// How long a status line stays in the readout, in seconds
constexpr float STATUS_SECONDS = 3.0f;

/** F5 saves the game, F9 loads it. */
static void handle_save_keys();

/** The settings a new world is generated with: world_cells_x by world_cells_y, from `seed`. */
static sim::TerrainSettings world_settings(uint32_t seed);

/** A seed nobody picked, for a world that is different every time. */
static uint32_t fresh_seed();

// What the readout says about the world on screen: the island's seed, shape,
// relief and biome, or that it was loaded
static std::string world_description;

/** The box round the land cells, in voxels of the map's x and y. */
struct LandBox {
    int min_x, min_y, max_x, max_y;
    bool any() const { return max_x > min_x; }
};

/**
 * Lays the sea floor over the ocean cells, which the voxel map does not hold,
 * and puts the shadow window over the land: all of it when it fits one window
 * (MAX_WORLD_VOLUME_SIDE), otherwise a window's worth round `focus` (a box in
 * the map's voxels, x and y), or round the middle of the land with no focus.
 * After a new world, a load, and an island placed.
 */
static LandBox refresh_land(const Rectangle* focus);

/** What the presentation does about the last step's events: islands placed, and refused. */
static void present_world_events();

void global::init() {
    SetConfigFlags(FLAG_MSAA_4X_HINT);  // Enable Multi Sampling Anti Aliasing 4x (if available)
    // Warnings and errors only. raylib logs every mesh it uploads to or frees
    // from VRAM at LOG_INFO, which a map of thousands of chunks turns into a
    // flood, along with its start-up report on the GL context and every shader
    // and texture. The game's own routine messages go with them; set LOG_LEVEL
    // lower to see everything again.
    // It has to go through Init(): raylib-cpp's Init() sets the log level
    // itself, to LOG_ALL unless told otherwise, so a SetTraceLogLevel() before
    // it is undone. A flags argument of 0 leaves the MSAA flag above alone.
    raylib::Window::Init(1600, 900, "business game", 0, LOG_LEVEL);

    // How near and far the camera sees, which BeginMode3D() builds every
    // projection with. raylib's own 0.01 to 1000 cut the world off a thousand
    // voxels out, well short of its far side, and its tiny near plane spent
    // the depth buffer's precision on the first metre, leaving the distance
    // coarse enough for the water to fight the ground. The fog ends short of
    // the far plane, so the edge is never seen.
    rlSetClipPlanes(CAMERA_NEAR, CAMERA_FAR);

    // Escape is a tool's "give up on this selection" key - the attachment menu
    // and the grid transform menu both offer it - so it cannot also be the one
    // that closes the window. The window button is the way out now.
    SetExitKey(KEY_NULL);

    root_view = std::make_unique<ViewNode>(nullptr);
    // A new island every time the game starts
    world_seed = fresh_seed();
    simulation = std::make_unique<sim::Simulation>(SIMULATION_SEED, world_settings(world_seed));

    voxel_shader = loadAndPatchShader("../resources/shaders/lighting", 2, MAX_GRID_VOLUMES);
    voxel_shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(voxel_shader, "viewPos");

    // Ambient light level (some basic lighting)
    int ambientLoc = GetShaderLocation(voxel_shader, "ambient");
    SetShaderValue(voxel_shader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Voxels. The map is sized to hold the simulation's terrain, which
    // start_world() draws into it, a block as a cube of voxels, once the
    // water exists too.
    root_view->add_child(std::make_unique<VoxelView>(root_view.get(), &voxel_shader,
                                                     terrain_voxel_size(simulation->terrain())));
    voxel_view = static_cast<VoxelView *>(root_view->child.get());
    voxel_view->fog = &fog;

    // Water over the whole map. A sibling of the VoxelView, after it, as it
    // needs the voxels' depth in the buffer to be hidden behind the terrain,
    // and before the UI, which goes on top of both.
    const Int2 map_size = voxel_view->game_map->get_size();
    auto water_node = std::make_unique<WaterView>(root_view.get(), &voxel_view->camera,
        map_size.x, map_size.y, "../resources/shaders/water");
    water_view = water_node.get();
    // Its level is the simulation's, read through the global rather than a
    // captured pointer so a loaded game (assigned into it) is read too
    water_view->level_source = [] { return static_cast<int>(simulation->water_level()); };
    // The same fog as the voxels
    water_view->fog = &fog;
    // Drawn through the same world matrix as the map's chunks, so the two stay
    // together wherever the map is put (see voxel_model_matrix())
    water_view->terrain_matrix = [] {
        return transform_to_matrix(voxel_view->game_map->get_world_transform());
    };
    root_view->add_child(std::move(water_node));

    // The terrain drawn in, and the water, the sea floor, the shadows and the
    // camera fitted to it
    start_world();

    // The simulation's vehicles, made visible. Every asset is put on the map's
    // colours, and the placeholder cars come in three of them.
    assets = std::make_unique<AssetRegistry>(voxel_view->game_map->voxel_colours);
    assets->register_builder("car.maroon", placeholder_car_builder(10), PLACEHOLDER_CAR_PIVOT);
    assets->register_builder("car.blue", placeholder_car_builder(4), PLACEHOLDER_CAR_PIVOT);
    assets->register_builder("car.orange", placeholder_car_builder(5), PLACEHOLDER_CAR_PIVOT);
    entities = std::make_unique<EntityManager>(voxel_view, assets.get(), voxel_view);

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

    // Under the title: which world this is, and for a few seconds after a save
    // or a load, how it went
    auto sim_readout_node = std::make_unique<UILabel>(ui_view, "",
        Rectangle{0.0f, 54.0f, 0.0f, 0.0f}, Anchor::TOP_CENTER);
    auto sim_readout = sim_readout_node.get();
    sim_readout->font_size = 16.0f;
    sim_readout->background = ui_view->style.background;
    ui_view->add_child(std::move(sim_readout_node));
    add_script(std::make_unique<LambdaScript>("simulation readout", [sim_readout](const float delta) {
        sim_readout->text = world_description;
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

    // Where the fog starts and where it is total, in world units from the
    // camera. Both shaders read global::fog every frame. The end is kept short
    // of the far clip plane, or the world's edge would show through it.
    shader_menu->add_value_row("fog start", &fog.start, 100.0f, 0.0f, static_cast<float>(CAMERA_FAR), 0, {});
    shader_menu->add_value_row("fog end", &fog.end, 100.0f, 100.0f, static_cast<float>(CAMERA_FAR), 0, {});

    ui_view->add_child(std::move(shader_menu_node));

    ui_view->add_child(std::make_unique<UIButton>(ui_view, "Shader Menu",
        [shader_menu] { shader_menu->visible = !shader_menu->visible; },
        Rectangle{UI_MARGIN, UI_MARGIN, 0.0f, UI_BUTTON_HEIGHT}, Anchor::TOP_LEFT));

    // The game's settings, hidden until F4 or its button. Its button and panel
    // sit one shader menu panel's width to the right of the shader menu, so the
    // two panels can be open at once without covering each other.
    const float settings_x = UI_MARGIN + UINumberRow::row_size().x + 2.0f * NUMBER_ROW_GAP + UI_BUTTON_GAP;
    auto settings_menu_node = std::make_unique<GameSettingsMenu>(ui_view);
    auto settings_menu = settings_menu_node.get();
    settings_menu->bounds = Rectangle{settings_x, UI_MARGIN + UI_BUTTON_HEIGHT + UI_BUTTON_GAP, 0.0f, 0.0f};

    // The highest voxel layer the water fills, up to the top of the map. It is
    // the simulation's, so the row reads it from there and changes it with a
    // command, which lands on the next tick.
    settings_menu->add_int_row("water level",
        [] { return static_cast<int>(simulation->water_level()); },
        [](const int level) { commands.submit(std::make_unique<sim::SetWaterLevel>(level)); },
        1, sim::MIN_WATER_LEVEL, voxel_view->game_map->get_height() - 1);

    // How far from the camera vehicles are drawn. A getter and a setter rather
    // than a pointer to the radius: the entities go before the view tree in
    // shutdown(), and the row must not hold onto them. The unrealise radius
    // follows at the same margin, so the two never cross.
    settings_menu->add_int_row("vehicle distance",
        [] { return static_cast<int>(entities->realize_radius); },
        [](const int radius) {
            entities->realize_radius = static_cast<float>(radius);
            entities->unrealize_radius = static_cast<float>(radius) + UNREALIZE_MARGIN;
        },
        16, 16, 1024);

    // How many cells the next new world is on each side. They change nothing
    // until "New Island" makes one.
    settings_menu->add_int_row("world cells x",
        [] { return world_cells_x; },
        [](const int cells) { world_cells_x = cells; },
        1, 1, sim::MAX_WORLD_CELLS);
    settings_menu->add_int_row("world cells y",
        [] { return world_cells_y; },
        [](const int cells) { world_cells_y = cells; },
        1, 1, sim::MAX_WORLD_CELLS);

    // The camera speed multiplier: how fast the movement keys carry the camera,
    // 1 being the old 24 units a second. Labelled short to fit the row. The
    // pointer is safe, unlike the entities above: the VoxelView
    // is in the same view tree as the panel and goes with it.
    settings_menu->add_value_row("camera speed", &voxel_view->camera_speed_multiplier,
        0.25f, 0.25f, 20.0f, 2, {});

    ui_view->add_child(std::move(settings_menu_node));

    ui_view->add_child(std::make_unique<UIButton>(ui_view, "Game Settings",
        [settings_menu] { settings_menu->visible = !settings_menu->visible; },
        Rectangle{settings_x, UI_MARGIN, 0.0f, UI_BUTTON_HEIGHT}, Anchor::TOP_LEFT));

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

    // A whole new game: another island from another seed, in a world of the
    // size the settings menu says
    add_bottom_left_button("New World", [] { new_world(fresh_seed()); });

    // Another island in this world: choose it, then place it (IslandMenu)
    add_bottom_left_button("New Island", [] {
        if (island_menu != nullptr) island_menu->toggle();
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

    // The new island menu, on the left. Placing hands the island to the
    // simulation as a command; present_world_events() draws it when it lands.
    auto island_menu_node = std::make_unique<IslandMenu>(ui_view, voxel_view, simulation.get());
    island_menu = island_menu_node.get();
    island_menu->on_place = [](const int32_t cell_x, const int32_t cell_y, const sim::IslandSpec& spec) {
        commands.submit(std::make_unique<sim::PlaceIsland>(cell_x, cell_y, spec));
    };
    ui_view->add_child(std::move(island_menu_node));

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
        island_menu->cancel_placing();
    };
    editor->on_select = [transform_menu] {
        transform_menu->cancel();
        island_menu->cancel_placing();
    };
    island_menu->on_activate = [editor, transform_menu] {
        editor->select(NO_VOXEL_SELECTION);
        transform_menu->cancel();
    };

    // A free click on a car selects it, but only when neither of those two is
    // waiting on the click for itself
    vehicle_panel->world_click_taken = [transform_menu, editor] {
        return transform_menu->is_active() || editor->is_active() || island_menu->is_placing();
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
    water_view = nullptr;
    island_menu = nullptr;

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
        present_world_events();
        tick_accumulator -= tick_length;
        ticks_run++;
    }
    if (ticks_run == MAX_TICKS_PER_FRAME) tick_accumulator = 0.0f;

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
        // The fog's colour, so ground faded all the way into it is the sky
        ClearBackground(fog.colour);
        root_view->render();
    }
    EndDrawing();
}

static sim::TerrainSettings world_settings(const uint32_t seed) {
    sim::TerrainSettings settings;
    settings.cells_x = global::world_cells_x;
    settings.cells_y = global::world_cells_y;
    settings.seed = seed;
    return settings;
}

static uint32_t fresh_seed() {
    // The presentation's to pick, not the simulation's: the seed is all the
    // simulation is given, and the same seed gives the same world everywhere
    return std::random_device{}();
}

void global::new_world(const uint32_t seed) {
    // The same order a load goes in: nothing on screen outlives the old game
    entities->clear();
    world_seed = seed;
    // Assigned into the existing objects rather than replacing them, as the
    // vehicle panel holds pointers to both
    *simulation = sim::Simulation(SIMULATION_SEED, world_settings(seed));
    commands = sim::CommandQueue{};
    tick_accumulator = 0.0f;
    start_world();

    const sim::IslandSpec spec = sim::random_island_spec(seed);
    TraceLog(LOG_INFO, "WORLD: %i x %i cells, seed %u: %s, %s, %s island", simulation->terrain().cells_x(),
             simulation->terrain().cells_y(), seed, sim::island_shape_name(spec.shape),
             sim::elevation_name(spec.elevation), sim::biome_name(spec.biome));
}

void global::start_world() {
    const sim::Terrain& terrain = simulation->terrain();
    VoxelMap& map = *voxel_view->game_map;

    // The map resized to the terrain, which may be a loaded game's of another
    // size, and the land drawn into it
    const Int3 size = terrain_voxel_size(terrain);
    map.resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y), static_cast<uint32_t>(size.z));
    build_terrain_voxels(map, terrain);

    // The water over the whole world, and the sea floor and the shadows over
    // what is ocean and what is land
    water_view->set_area(size.x, size.y);
    const LandBox land = refresh_land(nullptr);
    const bool any_land = land.any();
    const int cell_voxels = terrain.cell_blocks() * BLOCK_VOXELS;
    const int land_min_x = land.min_x, land_min_y = land.min_y, land_max_x = land.max_x, land_max_y = land.max_y;

    // The camera over the middle of the land, or of the world, far enough
    // back to see the whole island. The map's matrix carries the point from
    // the map's space (X grid x, Y up, Z grid y) into the world.
    const float centre_x = any_land ? 0.5f * static_cast<float>(land_min_x + land_max_x) : 0.5f * size.x;
    const float centre_z = any_land ? 0.5f * static_cast<float>(land_min_y + land_max_y) : 0.5f * size.y;
    const float extent = any_land ? static_cast<float>(std::max(land_max_x - land_min_x, land_max_y - land_min_y))
                                  : static_cast<float>(cell_voxels);
    const Matrix map_matrix = transform_to_matrix(map.get_world_transform());
    const Vector3 target = Vector3Transform(
        Vector3{centre_x, static_cast<float>(simulation->water_level()), centre_z}, map_matrix);
    const float back = 0.45f * extent + 40.0f;
    voxel_view->camera.target = target;
    voxel_view->camera.position = Vector3{target.x - back, target.y + back, target.z - back};

    world_description = any_land ? TextFormat("seed %u", world_seed) : "no island";
    if (any_land) {
        const sim::IslandSpec spec = sim::random_island_spec(world_seed);
        world_description += TextFormat(": %s, %s, %s", sim::island_shape_name(spec.shape),
                                        sim::elevation_name(spec.elevation), sim::biome_name(spec.biome));
    }
}

static LandBox refresh_land(const Rectangle* focus) {
    using namespace global;
    const sim::Terrain& terrain = simulation->terrain();
    VoxelMap& map = *voxel_view->game_map;
    const int cell_voxels = terrain.cell_blocks() * BLOCK_VOXELS;

    LandBox land{map.get_size().x, map.get_size().y, 0, 0};
    std::vector<WaterChunk> floor;
    for (int cell_y = 0; cell_y < terrain.cells_y(); ++cell_y) {
        for (int cell_x = 0; cell_x < terrain.cells_x(); ++cell_x) {
            const int x = cell_x * cell_voxels;
            const int y = cell_y * cell_voxels;
            if (terrain.is_ocean_cell(cell_x, cell_y)) {
                floor.push_back(WaterChunk{static_cast<float>(x), static_cast<float>(y),
                                           static_cast<float>(cell_voxels), static_cast<float>(cell_voxels)});
                continue;
            }
            land.min_x = std::min(land.min_x, x);
            land.min_y = std::min(land.min_y, y);
            land.max_x = std::max(land.max_x, x + cell_voxels);
            land.max_y = std::max(land.max_y, y + cell_voxels);
        }
    }
    water_view->set_floor(std::move(floor), static_cast<float>(terrain.sea_floor() * BLOCK_VOXELS));

    // Shadows over the land only. The ocean is flat and drawn by the water
    // view, so nothing there casts or catches a shadow from the volume. Land
    // wider than one window keeps the window round the focus: islands far
    // from it cast no shadow until one near them is placed.
    if (!land.any()) {
        voxel_view->set_volume_window(Int3{0, 0, 0}, Int3{CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE});
        return land;
    }
    const auto fit = [](const int lo, const int hi, const float focus_centre, int* origin, int* extent) {
        if (hi - lo <= MAX_WORLD_VOLUME_SIDE) {
            *origin = lo;
            *extent = hi - lo;
            return;
        }
        const int centre = static_cast<int>(focus_centre);
        *origin = std::clamp(centre - MAX_WORLD_VOLUME_SIDE / 2, lo, hi - MAX_WORLD_VOLUME_SIDE);
        *extent = MAX_WORLD_VOLUME_SIDE;
    };
    int origin_x = 0, origin_y = 0, extent_x = 0, extent_y = 0;
    fit(land.min_x, land.max_x,
        focus != nullptr ? focus->x + focus->width * 0.5f : 0.5f * static_cast<float>(land.min_x + land.max_x),
        &origin_x, &extent_x);
    fit(land.min_y, land.max_y,
        focus != nullptr ? focus->y + focus->height * 0.5f : 0.5f * static_cast<float>(land.min_y + land.max_y),
        &origin_y, &extent_y);
    // As tall as the land gets, the map's highest voxel now it is drawn
    voxel_view->set_volume_window(Int3{origin_x, origin_y, 0}, Int3{extent_x, extent_y, map.solid_top()});
    return land;
}

static void present_world_events() {
    using namespace global;
    for (const sim::Event& event : simulation->events()) {
        if (const auto* placed = std::get_if<sim::IslandPlaced>(&event)) {
            // The new land drawn into the map, cell by cell: the rest of the
            // map is as it was, and the chunks round the new cells are
            // marked for remeshing as they are drawn
            const sim::Terrain& terrain = simulation->terrain();
            const sim::Footprint footprint =
                sim::island_footprint(static_cast<sim::IslandShape>(placed->shape), placed->rotation);
            for (const sim::CellPos& c : footprint.cells) {
                draw_terrain_cell(*voxel_view->game_map, terrain, placed->cell_x + c.x, placed->cell_y + c.y);
            }

            // The sea floor and the shadows, the window round the new island
            const float cell_voxels = static_cast<float>(terrain.cell_blocks() * BLOCK_VOXELS);
            const Rectangle focus{
                static_cast<float>(placed->cell_x) * cell_voxels, static_cast<float>(placed->cell_y) * cell_voxels,
                static_cast<float>(footprint.width) * cell_voxels, static_cast<float>(footprint.height) * cell_voxels,
            };
            refresh_land(&focus);
            show_status("island placed");
        } else if (const auto* rejected = std::get_if<sim::CommandRejected>(&event)) {
            // The island was fine when it was placed, but something got there
            // first in the tick between
            if (rejected->reason == sim::RejectReason::InvalidIsland) show_status("the island did not fit there");
        }
    }
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

        // The terrain is the simulation's too, so the map is drawn again from
        // the loaded one, at the loaded one's size, with everything round it
        // fitted to it. The seed is not saved, so the readout cannot say it.
        start_world();
        world_description = "loaded game";

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
