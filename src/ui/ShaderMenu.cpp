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
      bias_texels(BIAS_TEXELS_DEFAULT),
      bias_slope_texels(BIAS_SLOPE_TEXELS_DEFAULT),
      bias_max_slope(BIAS_MAX_SLOPE_DEFAULT),
      ambient_level(global::ambient[0]),
      row_count(0)
{
    bias_texels_loc = GetShaderLocation(*shader, "biasTexels");
    bias_slope_texels_loc = GetShaderLocation(*shader, "biasSlopeTexels");
    bias_max_slope_loc = GetShaderLocation(*shader, "biasMaxSlope");
    ambient_loc = GetShaderLocation(*shader, "ambient");

    add_value_row("bias texels", &bias_texels, 0.25f, 0.0f, 16.0f, 2, [this] { push_bias(); });
    add_value_row("bias slope", &bias_slope_texels, 0.25f, 0.0f, 32.0f, 2, [this] { push_bias(); });
    add_value_row("bias max slope", &bias_max_slope, 0.5f, 0.0f, 64.0f, 2, [this] { push_bias(); });
    add_value_row("ambient", &ambient_level, 0.01f, 0.0f, 1.0f, 3, [this] { push_ambient(); });

    // The shader has no constants of its own any more, so the starting values
    // have to be sent before the first frame is drawn.
    push_bias();
    push_ambient();
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

void ShaderMenu::push_bias() const {
    SetShaderValue(*shader, bias_texels_loc, &bias_texels, SHADER_UNIFORM_FLOAT);
    SetShaderValue(*shader, bias_slope_texels_loc, &bias_slope_texels, SHADER_UNIFORM_FLOAT);
    SetShaderValue(*shader, bias_max_slope_loc, &bias_max_slope, SHADER_UNIFORM_FLOAT);
}

void ShaderMenu::push_ambient() const {
    // The shader takes a vec4, and the level drives the three colour channels
    global::ambient[0] = ambient_level;
    global::ambient[1] = ambient_level;
    global::ambient[2] = ambient_level;
    SetShaderValue(*shader, ambient_loc, global::ambient, SHADER_UNIFORM_VEC4);
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
