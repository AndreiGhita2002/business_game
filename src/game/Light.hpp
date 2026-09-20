//
// Created by Andrei Ghita on 06.10.2025.
//

#ifndef BUSINESS_GAME_LIGHT_HPP
#define BUSINESS_GAME_LIGHT_HPP
#include <vector>

#include <raylib.h>

// Light data
// taken from https://github.com/raysan5/raylib/blob/fbdf5e4fd2cb2ddd37d81e1c499797f3a2801ab5/examples/models/rlights.h#L46
enum LightType {
    DIRECTIONAL_LIGHT = 0,
    POINT_LIGHT = 1,
};

/**
 * One light, and the shader uniforms it writes to.
 *
 * A light carries no shadow map and no camera of its own: shadows are traced
 * through the world's voxels in the fragment shader, so a directional light is
 * only a direction, and there is nothing to fit a shadow box to. See
 * resources/shaders/lighting.fs.
 *
 * Point lights are lit but never shadowed.
 */
struct Light {
    unsigned int id{};
    int type{};
    bool enabled{true};

    // Where a directional light sits in the sky: an angle above the horizon and
    // an angle around the world's up axis, both in degrees. The direction the
    // light travels is worked out from these, so the two can never disagree,
    // and both are editable in the shader menu.
    float elevation{45.0f};
    float azimuth{135.0f};

    // Point lights only. A directional light has no position at all.
    Vector3 position{};

    Color color{WHITE};

    // Shader locations
    int enabled_loc{-1};
    int type_loc{-1};
    int direction_loc{-1};
    int position_loc{-1};
    int color_loc{-1};

    /**
     * The unit vector the light travels along, from the sky down onto the
     * world, so an elevation of 90 degrees gives straight down. A shadow ray
     * goes the other way.
     */
    Vector3 get_direction() const;

    /** Sends the light to the shader. Called every frame. */
    void update(Shader shader) const;

    /**
     * Factories: create, register the shader locations, and return the index of
     * the new light in `lights`.
     *
     * The vector must not grow afterwards, as the shader menu edits a light's
     * angles through a pointer into it.
     */
    static size_t create_directional(
        float elevation,
        float azimuth,
        Color color,
        const Shader& shader,
        std::vector<Light>* lights,
        unsigned int light_id
    );

    static size_t create_point(
        Vector3 position,
        Color color,
        const Shader& shader,
        std::vector<Light>* lights,
        unsigned int light_id
    );
};

#endif //BUSINESS_GAME_LIGHT_HPP
