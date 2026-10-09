//
// Created by Andrei Ghita on 25.08.2026.
//

#include "ShaderMenu.hpp"

#include "game/main.hpp"

std::string& ShaderMenu::get_view_type() {
    static std::string TYPE = SHADER_MENU_STR;
    return TYPE;
}

ShaderMenu::ShaderMenu(ViewNode* parent, raylib::Shader* shader)
    : SettingsPanel(parent, SHADER_MENU_TOGGLE_KEY),
      shader(shader),
      ambient_level(global::ambient[0]),
      ao_direct(AO_DIRECT_DEFAULT),
      shadow_steps_view(0.0f)
{
    ambient_loc = GetShaderLocation(*shader, "ambient");
    ao_direct_loc = GetShaderLocation(*shader, "aoDirectStrength");
    debug_shadow_steps_loc = GetShaderLocation(*shader, "debugShadowSteps");

    add_value_row("ambient", &ambient_level, 0.01f, 0.0f, 1.0f, 3, [this] { push_ambient(); });
    add_value_row("ao direct", &ao_direct, 0.05f, 0.0f, 1.0f, 2, [this] { push_ao_direct(); });
    add_value_row("step view", &shadow_steps_view, 1.0f, 0.0f, 1.0f, 0,
                  [this] { push_shadow_steps_view(); });

    // Sent before the first frame is drawn, as the shader holds no defaults of
    // its own for these
    push_ambient();
    push_ao_direct();
    push_shadow_steps_view();
}

void ShaderMenu::push_ambient() const {
    // The shader takes a vec4, and the level drives the three colour channels
    global::ambient[0] = ambient_level;
    global::ambient[1] = ambient_level;
    global::ambient[2] = ambient_level;
    SetShaderValue(*shader, ambient_loc, global::ambient, SHADER_UNIFORM_VEC4);
}

void ShaderMenu::push_ao_direct() const {
    SetShaderValue(*shader, ao_direct_loc, &ao_direct, SHADER_UNIFORM_FLOAT);
}

void ShaderMenu::push_shadow_steps_view() const {
    const int on = shadow_steps_view > 0.0f ? 1 : 0;
    SetShaderValue(*shader, debug_shadow_steps_loc, &on, SHADER_UNIFORM_INT);
}
