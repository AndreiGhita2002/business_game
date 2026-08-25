//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UILABEL_HPP
#define BUSINESS_GAME_UILABEL_HPP
#include <string>

#include "ui/UINode.hpp"

#define UI_LABEL_STR "UILabel"

/**
 * A run of text.
 *
 * Labels size themselves to their text by default, so they are usually built
 * with only a margin in `bounds`, as in Rectangle{8, 8, 0, 0}.
 * They do not block the mouse, so a label can be laid over anything.
 */
class UILabel : public UINode {
public:
    std::string text;

    // Left at 0 to take the size from the style
    float font_size;
    // Left at BLANK to take the colour from the style
    Color color;
    // Drawn behind the text when it is not fully transparent, which keeps a
    // label readable over a busy scene. Padded by the style's padding.
    Color background;

    // Where the text sits when the element is wider or taller than the text
    Anchor text_align;

    std::string& get_view_type() override;

    void draw() override;
    Vector2 measure() override;

    UILabel(ViewNode* parent, std::string text,
            Rectangle bounds = Rectangle{}, Anchor anchor = Anchor::TOP_LEFT);

private:
    // The size of the text on its own, without the background padding
    Vector2 text_size();
};

#endif //BUSINESS_GAME_UILABEL_HPP
