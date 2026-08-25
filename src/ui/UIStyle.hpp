//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UISTYLE_HPP
#define BUSINESS_GAME_UISTYLE_HPP
#include "raylib.h"

/**
 * The shared look of the UI elements.
 * Owned by UIView; UINodes read it through UINode::style().
 */
struct UIStyle {
    Font font;
    float font_size;
    float font_spacing;
    // Space kept between an element's edge and its content
    float padding;

    Color background;
    Color foreground;       // text and icons
    Color accent;           // hovered elements
    Color accent_pressed;   // held elements
    Color border;
    float border_thickness;
};

/**
 * The style used when nothing else is set.
 * Must only be called after the window has been initialised,
 * as it asks raylib for the default font.
 */
inline UIStyle default_ui_style() {
    return UIStyle{
        GetFontDefault(),
        20.0f,  // font_size
        2.0f,   // font_spacing
        6.0f,   // padding
        Color{40, 44, 52, 220},     // background
        RAYWHITE,                   // foreground
        Color{70, 78, 92, 235},     // accent
        Color{100, 110, 128, 255},  // accent_pressed
        Color{20, 22, 26, 255},     // border
        1.0f,                       // border_thickness
    };
}

#endif //BUSINESS_GAME_UISTYLE_HPP
