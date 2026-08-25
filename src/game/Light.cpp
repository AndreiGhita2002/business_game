//
// Created by Andrei Ghita on 06.10.2025.
//

#include "Light.hpp"

#include <rlgl.h>

size_t Light::create(
        LightType type,
        Vector3 pos,
        Vector3 target,
        Color color,
        const Shader& shader,
        std::vector<Light>* lights,
        unsigned int next_light_id
) {
    Light& light = lights->emplace_back();

    light.enabled = true;
    light.type = type == DIRECTIONAL_LIGHT ? 0 : 1;
    light.position = pos;
    light.target = target;
    light.color = color;

    light.id = next_light_id;
    light.enabled_loc  = GetShaderLocation(shader, TextFormat("lights[%i].enabled",  light.id));
    light.type_loc     = GetShaderLocation(shader, TextFormat("lights[%i].type",     light.id));
    light.position_loc = GetShaderLocation(shader, TextFormat("lights[%i].position", light.id));
    light.target_loc   = GetShaderLocation(shader, TextFormat("lights[%i].target",   light.id));
    light.color_loc    = GetShaderLocation(shader, TextFormat("lights[%i].color",    light.id));
    // L.attenuationLoc = GetShaderLocation(shader, TextFormat("lights[%i].attenuation", L.id));
    light.texture_loc = light.id + 10; // the 10 is kinda arbitrary
    light.vp_loc = GetShaderLocation(shader, TextFormat("lightVP%i", light.id));
    light.shadow_map_loc = GetShaderLocation(shader, TextFormat("shadowMap%i", light.id));
    light.shadow_texel_loc = GetShaderLocation(shader, TextFormat("lights[%i].shadowTexelDepth", light.id));

    //todo find a better camera configuration for lights
    // For an orthographic camera raylib reads fovy as the height of the view
    // box in world units, not as an angle, so this is the width of the area
    // that receives shadows at all. Anything outside it is drawn fully lit.
    // Bigger box, more coverage, blockier shadows: 1024 texels spread over 128
    // units still leaves 8 texels per voxel.
    light.light_camera = {
        light.position,
        light.target,
        { 0.0f, 1.0f, 0.0f },
        128.0f,
        CAMERA_ORTHOGRAPHIC
    };

    // Shadow Map
    light.shadow_map = new raylib::RenderTexture2D();
    auto fbo = rlLoadFramebuffer(); // load an empty framebuffer
    light.shadow_map->id = fbo;
    light.shadow_map->texture.width = SHADOWMAP_RESOLUTION;
    light.shadow_map->texture.height = SHADOWMAP_RESOLUTION;
    if (fbo > 0) {
        rlEnableFramebuffer(fbo);

        // Create depth texture
        light.shadow_map->depth.id = rlLoadTextureDepth(SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION, false);
        light.shadow_map->depth.width = SHADOWMAP_RESOLUTION;
        light.shadow_map->depth.height = SHADOWMAP_RESOLUTION;
        // light.shadow_map->depth.format = PIXELFORMAT_COMPRESSED_ETC2_RGB; // Already written by rlLoadTextureDepth
        light.shadow_map->depth.mipmaps = 1;

        // Attach depth texture to framebuffer
        rlFramebufferAttach(fbo, light.shadow_map->depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        // Check if framebuffer is complete with attachments
        if (rlFramebufferComplete(fbo) > 0)
            TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", fbo);
        else
            TRACELOG(LOG_WARNING, "FBO: [ID %i] Framebuffer object created unsuccessfully", fbo);

        rlDisableFramebuffer();
    }
    else TraceLog(LOG_WARNING, "FBO: Shadowmap framebuffer object can not be created!");

    TraceLog(LOG_DEBUG, "[Light] %zu: unit=%d locSamp=%d fbo=%u depthTex=%u pos=(%.2f,%.2f,%.2f) tgt=(%.2f,%.2f,%.2f)",
        light.id, light.texture_loc, light.shadow_map_loc,
        light.shadow_map->id, light.shadow_map->depth.id,
        light.position.x, light.position.y, light.position.z,
        light.target.x, light.target.y, light.target.z);

    return lights->size() - 1;
}

void Light::update(Shader shader) {
    // Move light camera
    light_camera.position = position;
    light_camera.target = target;

    // Send to shader light enabled state and type
    int s_enabled = enabled ? 1 : 0;
    SetShaderValue(shader, enabled_loc, &s_enabled, SHADER_UNIFORM_INT);
    int s_type = (type == POINT_LIGHT) ? 1 : 0;
    SetShaderValue(shader, type_loc, &s_type, SHADER_UNIFORM_INT);

    // Send to shader light position values
    float s_position[3] = {position.x, position.y, position.z};
    SetShaderValue(shader, position_loc, s_position, SHADER_UNIFORM_VEC3);

    // Send to shader light target position values
    float s_target[3] = {target.x, target.y, target.z};
    SetShaderValue(shader, target_loc, s_target, SHADER_UNIFORM_VEC3);

    // Send to shader light color values
    Vector4 s_color = { color.r/255.f, color.g/255.f, color.b/255.f, color.a/255.f };
    SetShaderValue(shader, color_loc, &s_color, SHADER_UNIFORM_VEC4);

    // One shadow map texel, in the depth units the shader compares against. The
    // shader scales its bias by this, so resizing the light's box (or holding O
    // and P) keeps the bias right without any constants being retuned.
    const float texel_world = light_camera.fovy / static_cast<float>(SHADOWMAP_RESOLUTION);
    const float texel_depth = texel_world / static_cast<float>(SHADOW_FAR - SHADOW_NEAR);
    SetShaderValue(shader, shadow_texel_loc, &texel_depth, SHADER_UNIFORM_FLOAT);
}

Light::~Light() {
    if (shadow_map) {
        // Only unload if it looks valid
        if (shadow_map->id != 0) {
            UnloadRenderTexture(*shadow_map);
        }
        delete shadow_map;
        shadow_map = nullptr;
    }
    else { TraceLog(LOG_DEBUG, "[Light] %i: shadow map already freed!", id); }
}

// Move constructor
Light::Light(Light&& other) noexcept
    : id(other.id)
    , type(other.type)
    , enabled(other.enabled)
    , position(other.position)
    , target(other.target)
    , color(other.color)
    , attenuation(other.attenuation)
    , light_camera(other.light_camera)
    , shadow_map(other.shadow_map) // take ownership
    , light_view_proj(other.light_view_proj)
    , enabled_loc(other.enabled_loc)
    , type_loc(other.type_loc)
    , position_loc(other.position_loc)
    , target_loc(other.target_loc)
    , color_loc(other.color_loc)
    , attenuation_loc(other.attenuation_loc)
    , vp_loc(other.vp_loc)
    , shadow_map_loc(other.shadow_map_loc)
    , texture_loc(other.texture_loc)
    , shadow_texel_loc(other.shadow_texel_loc)
{
    other.shadow_map = nullptr;
}

// Move assignment
Light& Light::operator=(Light&& other) noexcept {
    if (this != &other) {
        // Release current ownership first
        if (shadow_map) {
            if (shadow_map->id != 0) {
                UnloadRenderTexture(*shadow_map);
            }
            delete shadow_map;
        }
        // Take ownership of the render texture pointer
        shadow_map   = other.shadow_map;
        other.shadow_map = nullptr;

        // Copies
        id = other.id;
        type = other.type;
        enabled = other.enabled;
        position = other.position;
        target = other.target;
        color = other.color;
        attenuation = other.attenuation;
        light_camera = other.light_camera;
        light_view_proj = other.light_view_proj;
        enabled_loc = other.enabled_loc;
        type_loc = other.type_loc;
        position_loc = other.position_loc;
        target_loc = other.target_loc;
        color_loc = other.color_loc;
        attenuation_loc = other.attenuation_loc;
        vp_loc = other.vp_loc;
        shadow_map_loc = other.shadow_map_loc;
        texture_loc = other.texture_loc;
        shadow_texel_loc = other.shadow_texel_loc;
    }
    return *this;
}
