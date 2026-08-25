//
// Created by Andrei Ghita on 25.08.2026.
//

#include "UIButton.hpp"

#include <utility>

std::string& UIButton::get_view_type() {
    static std::string TYPE = UI_BUTTON_STR;
    return TYPE;
}

UIButton::UIButton(ViewNode* parent, std::string text, std::function<void()> action,
                   const Rectangle bounds, const Anchor anchor)
    : UINode(parent, bounds, anchor), text(std::move(text)),
      action(std::move(action)), font_size(0.0f)
{}

Vector2 UIButton::measure() {
    const UIStyle& s = style();
    const float size = font_size > 0.0f ? font_size : s.font_size;
    const Vector2 text_size = MeasureTextEx(s.font, text.c_str(), size, s.font_spacing);

    // Wider padding on the sides, which reads better on a text button
    return Vector2{
        text_size.x + s.padding * 4.0f,
        text_size.y + s.padding * 2.0f
    };
}

void UIButton::draw() {
    const UIStyle& s = style();

    Color fill = s.background;
    if (pressed) fill = s.accent_pressed;
    else if (hovered) fill = s.accent;

    DrawRectangleRec(screen_rect, fill);
    if (s.border_thickness > 0.0f)
        DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);

    const float size = font_size > 0.0f ? font_size : s.font_size;
    const Vector2 text_size = MeasureTextEx(s.font, text.c_str(), size, s.font_spacing);
    const Vector2 position = anchor_in_rect(text_size, screen_rect, Anchor::CENTER);

    DrawTextEx(s.font, text.c_str(), position, size, s.font_spacing, s.foreground);
}

void UIButton::on_click() {
    if (action) action();
}
