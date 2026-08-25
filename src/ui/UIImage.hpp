//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UIIMAGE_HPP
#define BUSINESS_GAME_UIIMAGE_HPP
#include "ui/UINode.hpp"

#define UI_IMAGE_STR "UIImage"

/**
 * A texture drawn into the element's rectangle.
 *
 * Sizing follows the aspect ratio of the texture: give `bounds` one of width or
 * height and leave the other at 0, and the missing one is worked out from the
 * texture. Leave both at 0 to get the texture's own size times `scale`.
 */
class UIImage : public UINode {
public:
    Texture2D texture;
    // Whether the texture is unloaded together with this element
    bool owns_texture;

    Color tint;
    // Applied to the texture size when `bounds` asks for no size at all
    float scale;
    // The part of the texture to draw. A zero width or height means all of it.
    Rectangle source;

    std::string& get_view_type() override;

    void draw() override;
    Vector2 measure() override;

    // Loads the texture from a file and unloads it when destroyed.
    // Paths are relative to the working directory, as with the shaders.
    UIImage(ViewNode* parent, const char* file_path,
            Rectangle bounds = Rectangle{}, Anchor anchor = Anchor::TOP_LEFT);

    // Draws a texture that is owned by someone else, and leaves it alone
    UIImage(ViewNode* parent, Texture2D texture,
            Rectangle bounds = Rectangle{}, Anchor anchor = Anchor::TOP_LEFT);

    ~UIImage() override;
};

#endif //BUSINESS_GAME_UIIMAGE_HPP
