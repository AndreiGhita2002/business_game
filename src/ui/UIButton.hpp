//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UIBUTTON_HPP
#define BUSINESS_GAME_UIBUTTON_HPP
#include <functional>
#include <string>

#include "ui/UINode.hpp"

#define UI_BUTTON_STR "UIButton"

/**
 * A rectangle with a label on it that runs `action` when it is clicked.
 *
 * The hovered and pressed states come from UIView, which only hands them to the
 * topmost element under the cursor, so overlapping buttons never both light up.
 * A click only counts when the button goes down and back up on the same element.
 */
class UIButton : public UINode {
public:
    std::string text;
    // Run on a completed click. Subclasses may override on_click() instead.
    std::function<void()> action;

    // Left at 0 to take the size from the style
    float font_size;

    std::string& get_view_type() override;

    void draw() override;
    Vector2 measure() override;
    void on_click() override;

    UIButton(ViewNode* parent, std::string text, std::function<void()> action = {},
             Rectangle bounds = Rectangle{}, Anchor anchor = Anchor::TOP_LEFT);
};

#endif //BUSINESS_GAME_UIBUTTON_HPP
