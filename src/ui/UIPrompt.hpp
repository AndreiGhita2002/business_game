//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_UIPROMPT_HPP
#define BUSINESS_GAME_UIPROMPT_HPP
#include <string>

#include "raylib.h"
#include "ui/UIStyle.hpp"

// How far above the middle of the window a prompt sits, in pixels
#define PROMPT_Y_OFFSET (-140.0f)

// The prompt is bigger than ordinary UI text, as it is the one thing the user
// is meant to read while looking at the world rather than at the panels
#define PROMPT_FONT_SCALE 1.5f

/**
 * One line of instruction drawn across the middle of the window, for a tool
 * that is waiting on the user to click something in the world ("Select Anchor
 * Voxel" and the like).
 *
 * A free function rather than a UINode: the tools that need it are panels
 * pinned to an edge of the screen, and a child of one of those is laid out
 * inside the panel rather than in the middle of the window. It draws where it
 * is told instead, and takes part in no layout or hit testing at all - a prompt
 * is never something to click.
 *
 * Header only, and inline: it is a handful of draw calls over the style, with
 * no state of its own to keep anywhere.
 *
 * Must be called from inside a draw pass, so from a UINode::draw().
 */
inline void draw_screen_prompt(const UIStyle& style, const std::string& text,
                               const float y_offset = PROMPT_Y_OFFSET) {
    if (text.empty()) return;

    const float font_size = style.font_size * PROMPT_FONT_SCALE;
    const Vector2 size = MeasureTextEx(style.font, text.c_str(), font_size, style.font_spacing);

    const Vector2 origin = Vector2{
        (static_cast<float>(GetScreenWidth()) - size.x) * 0.5f,
        (static_cast<float>(GetScreenHeight()) - size.y) * 0.5f + y_offset,
    };

    // The plate behind it, so the text stays readable over the sky, which is
    // nearly the same colour as the foreground
    const Rectangle plate = Rectangle{
        origin.x - style.padding * 2.0f,
        origin.y - style.padding,
        size.x + style.padding * 4.0f,
        size.y + style.padding * 2.0f,
    };
    DrawRectangleRec(plate, style.background);
    if (style.border_thickness > 0.0f)
        DrawRectangleLinesEx(plate, style.border_thickness, style.border);

    DrawTextEx(style.font, text.c_str(), origin, font_size, style.font_spacing, style.foreground);
}

#endif //BUSINESS_GAME_UIPROMPT_HPP
