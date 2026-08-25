//
// Created by Andrei Ghita on 25.08.2026.
//

#include "UILabel.hpp"

#include <utility>

std::string& UILabel::get_view_type() {
    static std::string TYPE = UI_LABEL_STR;
    return TYPE;
}

UILabel::UILabel(ViewNode* parent, std::string text, const Rectangle bounds, const Anchor anchor)
    : UINode(parent, bounds, anchor), text(std::move(text)),
      font_size(0.0f), color(BLANK), background(BLANK), text_align(Anchor::CENTER_LEFT)
{
    // Plain text should not swallow clicks meant for the world behind it
    blocks_mouse = false;
}

Vector2 UILabel::text_size() {
    const UIStyle& s = style();
    const float size = font_size > 0.0f ? font_size : s.font_size;
    return MeasureTextEx(s.font, text.c_str(), size, s.font_spacing);
}

Vector2 UILabel::measure() {
    Vector2 size = text_size();
    if (background.a > 0) {
        const float padding = style().padding;
        size.x += padding * 2.0f;
        size.y += padding * 2.0f;
    }
    return size;
}

void UILabel::draw() {
    const UIStyle& s = style();

    if (background.a > 0) DrawRectangleRec(screen_rect, background);

    // The text is aligned inside the rectangle, minus the background padding
    Rectangle content = screen_rect;
    if (background.a > 0) {
        content.x += s.padding;
        content.y += s.padding;
        content.width -= s.padding * 2.0f;
        content.height -= s.padding * 2.0f;
    }

    const Vector2 position = anchor_in_rect(text_size(), content, text_align);
    DrawTextEx(s.font, text.c_str(), position,
               font_size > 0.0f ? font_size : s.font_size,
               s.font_spacing,
               color.a > 0 ? color : s.foreground);
}
