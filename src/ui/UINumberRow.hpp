//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UINUMBERROW_HPP
#define BUSINESS_GAME_UINUMBERROW_HPP
#include <functional>
#include <string>

#include "ui/UINode.hpp"

#define UI_NUMBER_ROW_STR "UINumberRow"

// Layout of a row, in pixels
#define NUMBER_ROW_HEIGHT 22.0f
#define NUMBER_ROW_LABEL_WIDTH 104.0f
#define NUMBER_ROW_BUTTON_WIDTH 20.0f
#define NUMBER_ROW_VALUE_WIDTH 56.0f
#define NUMBER_ROW_GAP 4.0f
#define NUMBER_ROW_FONT_SIZE 14.0f

/**
 * One editable number, drawn as `label [-] value [+]`.
 *
 * The row does not own the number: it holds a pointer to one that lives
 * somewhere else, which is what lets it drive a shader uniform, a light's
 * settings or anything else in place. Each row carries its own step, so a value
 * measured in tenths and one measured in tens both move sensibly.
 */
class UINumberRow final : public UINode {
public:
    std::string label;

    // The number being edited. Must outlive the row.
    float* value;
    float step;
    float min_value;
    float max_value;
    // Digits printed after the point
    int decimals;

    // Run after the value changes, for pushing it wherever it needs to go
    std::function<void()> on_change;

    std::string& get_view_type() override;

    void draw() override;
    Vector2 measure() override;

    // Adds `amount` steps to the value, clamps it, then reports the change
    void step_by(float amount);

    // The size every row has, for laying out a panel of them
    static Vector2 row_size();

    UINumberRow(ViewNode* parent, std::string label, float* value,
                float step, float min_value, float max_value,
                int decimals = 2, Rectangle bounds = Rectangle{});
};

#endif //BUSINESS_GAME_UINUMBERROW_HPP
