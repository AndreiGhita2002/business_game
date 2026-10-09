//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include <RenderTexture.hpp>
#include <Shader.hpp>
#include <vector>

#include "ViewNode.hpp"
#include "entity/AssetRegistry.hpp"
#include "entity/EntityManager.hpp"
#include "game/Script.hpp"
#include "game/Picking.hpp"
#include "game/Transform.hpp"
#include "ui/VoxelEditor.hpp"
#include "voxel/VoxelView.hpp"
#include "voxel/VoxelMap.hpp"
#include "water/WaterView.hpp"
#include "sim/Command.hpp"
#include "sim/Save.hpp"
#include "sim/Simulation.hpp"

namespace global {
    inline float render_distance = 128.0f;
    inline bool limit_render_distance = false;

    inline raylib::Shader voxel_shader;
    inline float ambient[4] = {0.06f, 0.06f, 0.06f, 1.0f};

    inline std::unique_ptr<ViewNode> root_view;
    // The view the scene is drawn by, owned by root_view's child chain
    inline VoxelView* voxel_view = nullptr;
    // The water over the map, also owned by root_view's child chain
    inline WaterView* water_view = nullptr;

    // --- Simulation ---
    // The game's state. Read through its const accessors, changed only by
    // queueing commands on `commands`, which mainLoop() hands to it a tick at
    // a time. See sim/Simulation.hpp.
    inline std::unique_ptr<sim::Simulation> simulation;
    inline sim::CommandQueue commands;

    // Game time not yet run as ticks, in seconds. Presentation state: the
    // simulation only ever learns "one more tick".
    inline float tick_accumulator = 0.0f;
    // How fast game time runs against real time. 0 pauses the simulation.
    inline float game_speed = 1.0f;
    // How many ticks the last frame ran, for the debug readout
    inline int ticks_last_frame = 0;

    // A line the readout shows for a few seconds, such as how a save went
    inline std::string status_message;
    inline float status_seconds_left = 0.0f;
    void show_status(const std::string& message);

    // --- Presentation of the simulation ---
    // What a vehicle's model name looks like, and the entities drawn for the
    // vehicles near the camera
    inline std::unique_ptr<AssetRegistry> assets;
    inline std::unique_ptr<EntityManager> entities;

    // Every running script, updated in order once per frame before the view
    // tree. Add them through add_script() so that they are started.
    inline std::vector<std::unique_ptr<Script>> scripts;

    // Main Functions, only called inside main
    static void init();
    static void mainLoop();
    static void shutdown();

    /** Takes the script, starts it, and runs it every frame from then on. */
    Script* add_script(std::unique_ptr<Script> script);

    std::string loadFile(const std::string& path);
    raylib::Shader loadAndPatchShader(const std::string& shader_path, int light_count,
                                      int max_grid_volumes);
}

// The transform maths lives in game/Transform.hpp and the ray casts in
// game/Picking.hpp, both included above so that everything that used to reach
// them through this header still can. Include the narrow one directly in new
// code: this header drags in the window, the shaders and the whole view tree.

#endif //BUSINESS_GAME_MAIN_HPP