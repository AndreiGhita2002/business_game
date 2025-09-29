//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_MAIN_HPP
#define BUSINESS_GAME_MAIN_HPP
#include <Camera3D.hpp>
#include <RenderTexture.hpp>
#include <Shader.hpp>
#include <vector>
#include "voxel/VoxelMap.hpp"

#define SHADOWMAP_RESOLUTION 1024

// Light data
// taken from https://github.com/raysan5/raylib/blob/fbdf5e4fd2cb2ddd37d81e1c499797f3a2801ab5/examples/models/rlights.h#L46
enum LightType {
    DIRECTIONAL_LIGHT = 0,
    POINT_LIGHT = 1,
};

struct Light {
    unsigned int id{};
    int type{};
    bool enabled{true};
    Vector3 position{};
    Vector3 target{};
    Color color{WHITE};
    float attenuation{1.0f}; // not used

    Camera3D light_camera;
    raylib::RenderTexture2D* shadow_map = nullptr;
    Matrix light_view_proj{};

    // Shader locations
    int enabled_loc{-1};
    int type_loc{-1};
    int position_loc{-1};
    int target_loc{-1};
    int color_loc{-1};
    int attenuation_loc{-1}; // not used
    int vp_loc{-1};
    int shadow_map_loc{-1};
    int texture_loc{-1};

    void update(Shader shader);

    // Factory: creates, initializes, registers, and returns the index of the Light.
    // Side Effects: edits global::lights and global::next_light_id
    static size_t create(LightType type, Vector3 pos, Vector3 target, Color color, const Shader& shader);

    Light() = default;
    ~Light();

    Light(Light&& other) noexcept; // implement this
    Light& operator=(Light&& other) noexcept; // and implement this

    Light(const Light&) = delete;
    Light& operator=(const Light&) = delete;
};


namespace global {
    inline float voxel_scale = 0.2f;
    //TODO (Broken!) render circle is somehow not centered at the camera
    // has something to do with voxel_scale; it works perfectly if voxel_scale=0
    inline float render_distance = 128.0f;
    inline bool limit_render_distance = false;

    inline raylib::Camera camera;
    inline raylib::Shader voxel_shader;

    inline float ambient[4] = {0.06f, 0.06f, 0.06f, 1.0f};
    inline unsigned int next_light_id = 0;
    inline std::vector<Light> lights;
    inline size_t sun_light_id;
    inline size_t camera_light_id;
    inline bool move_camera_light = true;

    inline std::vector<VoxelGrid*> voxel_grids;
    inline VoxelMap* game_map;

    // Main Functions, only called inside main
    static void init();
    static void mainLoop();
    static void shutdown();

    // Update Functions, called every tick
    static void updateCamera();
    static void updateLights();
    static void updateVoxelMesh();

    // Drawing Functions
    // Should always be within a BeginMode3D()/EndMode3D() block.
    void drawVoxelScene();
    void drawVoxelModel(const ModelInfo& model_info);

    // Helper Functions
    bool isInRenderDistance(Vector3 v);
    std::string loadFile(const std::string& path);
    raylib::Shader loadAndPatchShader(const std::string& shader_path, int light_count);
}

Vector3 apply_transform(Vector3 v, const Transform &t);

#endif //BUSINESS_GAME_MAIN_HPP