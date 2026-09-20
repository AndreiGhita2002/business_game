//
// Created by Andrei Ghita on 06.10.2025.
//

#include "Light.hpp"

#include <cmath>

#include <raymath.h>

// The uniforms every light writes to, whatever its type
static void register_locations(Light& light, const Shader& shader) {
    light.enabled_loc   = GetShaderLocation(shader, TextFormat("lights[%i].enabled",   light.id));
    light.type_loc      = GetShaderLocation(shader, TextFormat("lights[%i].type",      light.id));
    light.direction_loc = GetShaderLocation(shader, TextFormat("lights[%i].direction", light.id));
    light.position_loc  = GetShaderLocation(shader, TextFormat("lights[%i].position",  light.id));
    light.color_loc     = GetShaderLocation(shader, TextFormat("lights[%i].color",     light.id));
}

size_t Light::create_directional(
        const float elevation,
        const float azimuth,
        const Color color,
        const Shader& shader,
        std::vector<Light>* lights,
        const unsigned int light_id
) {
    Light& light = lights->emplace_back();

    light.id = light_id;
    light.type = DIRECTIONAL_LIGHT;
    light.enabled = true;
    light.elevation = elevation;
    light.azimuth = azimuth;
    light.color = color;

    register_locations(light, shader);
    return lights->size() - 1;
}

size_t Light::create_point(
        const Vector3 position,
        const Color color,
        const Shader& shader,
        std::vector<Light>* lights,
        const unsigned int light_id
) {
    Light& light = lights->emplace_back();

    light.id = light_id;
    light.type = POINT_LIGHT;
    light.enabled = true;
    light.position = position;
    light.color = color;

    register_locations(light, shader);
    return lights->size() - 1;
}

Vector3 Light::get_direction() const {
    // Elevation is measured up from the horizon and azimuth around the world's
    // up axis, which is Y. The vector points the way the light travels, so it
    // aims downwards for anything above the horizon.
    const float el = elevation * DEG2RAD;
    const float az = azimuth * DEG2RAD;
    return Vector3{
        -cosf(el) * sinf(az),
        -sinf(el),
        -cosf(el) * cosf(az),
    };
}

void Light::update(const Shader shader) const {
    const int s_enabled = enabled ? 1 : 0;
    SetShaderValue(shader, enabled_loc, &s_enabled, SHADER_UNIFORM_INT);
    SetShaderValue(shader, type_loc, &type, SHADER_UNIFORM_INT);

    // Worked out from the angles every frame rather than stored, so that the
    // shader menu only ever has to move the angles
    const Vector3 direction = get_direction();
    const float s_direction[3] = {direction.x, direction.y, direction.z};
    SetShaderValue(shader, direction_loc, s_direction, SHADER_UNIFORM_VEC3);

    const float s_position[3] = {position.x, position.y, position.z};
    SetShaderValue(shader, position_loc, s_position, SHADER_UNIFORM_VEC3);

    const Vector4 s_color = {color.r/255.f, color.g/255.f, color.b/255.f, color.a/255.f};
    SetShaderValue(shader, color_loc, &s_color, SHADER_UNIFORM_VEC4);
}
