//
// Created by Andrei Ghita on 25.08.2026.
//

#include "ShaderMenu.hpp"

#include <utility>

#include "UINumberRow.hpp"
#include "game/main.hpp"

std::string& ShaderMenu::get_view_type() {
    static std::string TYPE = SHADER_MENU_STR;
    return TYPE;
}

ShaderMenu::ShaderMenu(ViewNode* parent, raylib::Shader* shader)
    : UINode(parent, Rectangle{16.0f, 16.0f, 0.0f, 0.0f}, Anchor::TOP_LEFT),
      visible(false), shader(shader),
      ambient_level(global::ambient[0]),
      ao_direct(AO_DIRECT_DEFAULT),
      shadow_steps_view(0.0f),
      row_count(0)
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

void ShaderMenu::add_value_row(std::string label, float* value, const float step,
                              const float min_value, const float max_value, const int decimals,
                              std::function<void()> on_change) {
    auto row = std::make_unique<UINumberRow>(
        this, std::move(label), value, step, min_value, max_value, decimals,
        Rectangle{
            NUMBER_ROW_GAP,
            NUMBER_ROW_GAP + static_cast<float>(row_count) * (NUMBER_ROW_HEIGHT + NUMBER_ROW_GAP),
            0.0f, 0.0f
        });
    row->on_change = std::move(on_change);
    add_child(std::move(row));
    row_count++;
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

Vector2 ShaderMenu::measure() {
    const Vector2 row = UINumberRow::row_size();
    return Vector2{
        row.x + 2.0f * NUMBER_ROW_GAP,
        static_cast<float>(row_count) * (NUMBER_ROW_HEIGHT + NUMBER_ROW_GAP) + NUMBER_ROW_GAP
    };
}

void ShaderMenu::update(const float delta_time) {
    if (!isEnabled) return;

    if (IsKeyPressed(SHADER_MENU_TOGGLE_KEY)) visible = !visible;

    ViewNode::update(delta_time);
}

void ShaderMenu::render() {
    if (!isEnabled) return;

    // Only the panel and its rows are held back while hidden. The sibling chain
    // still has to be walked, or every element added after this one would
    // disappear along with the menu.
    if (visible) {
        draw();
        if (child) child->render();
    }
    if (sibling) sibling->render();
}

UINode* ShaderMenu::hit_test(const Vector2 point) {
    // A hidden menu cannot be clicked, and must not swallow world clicks
    if (!visible) return nullptr;
    return UINode::hit_test(point);
}

void ShaderMenu::draw() {
    const UIStyle& s = style();

    DrawRectangleRec(screen_rect, s.background);
    if (s.border_thickness > 0.0f)
        DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);
}
