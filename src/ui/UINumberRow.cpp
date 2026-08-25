//
// Created by Andrei Ghita on 25.08.2026.
//

#include "UINumberRow.hpp"

#include <algorithm>
#include <utility>

#include "UIButton.hpp"

std::string& UINumberRow::get_view_type() {
    static std::string TYPE = UI_NUMBER_ROW_STR;
    return TYPE;
}

UINumberRow::UINumberRow(ViewNode* parent, std::string label, float* value,
                         const float step, const float min_value, const float max_value,
                         const int decimals, const Rectangle bounds)
    : UINode(parent, bounds), label(std::move(label)), value(value), step(step),
      min_value(min_value), max_value(max_value), decimals(decimals)
{
    const float minus_x = NUMBER_ROW_LABEL_WIDTH + NUMBER_ROW_GAP;
    const float plus_x = minus_x + NUMBER_ROW_BUTTON_WIDTH + NUMBER_ROW_GAP
                       + NUMBER_ROW_VALUE_WIDTH + NUMBER_ROW_GAP;

    auto minus = std::make_unique<UIButton>(this, "-", [this] { step_by(-1.0f); },
        Rectangle{minus_x, 0.0f, NUMBER_ROW_BUTTON_WIDTH, NUMBER_ROW_HEIGHT});
    minus->font_size = NUMBER_ROW_FONT_SIZE;
    add_child(std::move(minus));

    auto plus = std::make_unique<UIButton>(this, "+", [this] { step_by(1.0f); },
        Rectangle{plus_x, 0.0f, NUMBER_ROW_BUTTON_WIDTH, NUMBER_ROW_HEIGHT});
    plus->font_size = NUMBER_ROW_FONT_SIZE;
    add_child(std::move(plus));
}

void UINumberRow::step_by(const float amount) {
    if (value == nullptr) return;

    *value = std::clamp(*value + step * amount, min_value, max_value);
    if (on_change) on_change();
}

Vector2 UINumberRow::row_size() {
    return Vector2{
        NUMBER_ROW_LABEL_WIDTH + NUMBER_ROW_VALUE_WIDTH
            + 2.0f * NUMBER_ROW_BUTTON_WIDTH + 3.0f * NUMBER_ROW_GAP,
        NUMBER_ROW_HEIGHT
    };
}

Vector2 UINumberRow::measure() {
    return row_size();
}

void UINumberRow::draw() {
    const UIStyle& s = style();

    // Label, against the left edge
    const Rectangle label_rect = {
        screen_rect.x, screen_rect.y, NUMBER_ROW_LABEL_WIDTH, screen_rect.height
    };
    const Vector2 label_size = MeasureTextEx(s.font, label.c_str(), NUMBER_ROW_FONT_SIZE, s.font_spacing);
    DrawTextEx(s.font, label.c_str(),
               anchor_in_rect(label_size, label_rect, Anchor::CENTER_LEFT),
               NUMBER_ROW_FONT_SIZE, s.font_spacing, s.foreground);

    if (value == nullptr) return;

    // Value, between the two buttons
    const char* text = TextFormat("%.*f", decimals, *value);
    const Rectangle value_rect = {
        screen_rect.x + NUMBER_ROW_LABEL_WIDTH + NUMBER_ROW_GAP
            + NUMBER_ROW_BUTTON_WIDTH + NUMBER_ROW_GAP,
        screen_rect.y,
        NUMBER_ROW_VALUE_WIDTH,
        screen_rect.height
    };
    const Vector2 text_size = MeasureTextEx(s.font, text, NUMBER_ROW_FONT_SIZE, s.font_spacing);
    DrawTextEx(s.font, text,
               anchor_in_rect(text_size, value_rect, Anchor::CENTER),
               NUMBER_ROW_FONT_SIZE, s.font_spacing, s.foreground);
}
