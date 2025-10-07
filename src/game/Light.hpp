//
// Created by Andrei Ghita on 06.10.2025.
//

#ifndef BUSINESS_GAME_LIGHT_HPP
#define BUSINESS_GAME_LIGHT_HPP

#define SHADOWMAP_RESOLUTION 1024
#include <RenderTexture.hpp>
#include <Vector3.hpp>

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
    static size_t create(
        LightType type,
        Vector3 pos,
        Vector3 target,
        Color color,
        const Shader& shader,
        std::vector<Light>* lights,
        unsigned int next_light_id
    );

    Light() = default;
    ~Light();

    Light(Light&& other) noexcept; // implement this
    Light& operator=(Light&& other) noexcept; // and implement this

    Light(const Light&) = delete;
    Light& operator=(const Light&) = delete;
};


#endif //BUSINESS_GAME_LIGHT_HPP