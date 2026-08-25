//
// Created by Andrei Ghita on 25.08.2026.
//

#include "UIImage.hpp"

std::string& UIImage::get_view_type() {
    static std::string TYPE = UI_IMAGE_STR;
    return TYPE;
}

UIImage::UIImage(ViewNode* parent, const char* file_path, const Rectangle bounds, const Anchor anchor)
    : UINode(parent, bounds, anchor), texture(LoadTexture(file_path)), owns_texture(true),
      tint(WHITE), scale(1.0f), source(Rectangle{})
{
    if (texture.id == 0) {
        TraceLog(LOG_WARNING, "[UI] Could not load image: %s", file_path);
        return;
    }
    // UI images are usually shown far smaller than the file they came from, and
    // a plain bilinear downscale of a large texture aliases badly.
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
}

UIImage::UIImage(ViewNode* parent, const Texture2D texture, const Rectangle bounds, const Anchor anchor)
    : UINode(parent, bounds, anchor), texture(texture), owns_texture(false),
      tint(WHITE), scale(1.0f), source(Rectangle{})
{}

UIImage::~UIImage() {
    if (owns_texture && texture.id != 0) UnloadTexture(texture);
}

Vector2 UIImage::measure() {
    if (texture.id == 0 || texture.width == 0 || texture.height == 0)
        return Vector2{bounds.width, bounds.height};

    const auto width = static_cast<float>(texture.width);
    const auto height = static_cast<float>(texture.height);

    // Only one side given, so the other one keeps the aspect ratio
    if (bounds.width > 0.0f && bounds.height <= 0.0f)
        return Vector2{bounds.width, bounds.width * height / width};
    if (bounds.height > 0.0f && bounds.width <= 0.0f)
        return Vector2{bounds.height * width / height, bounds.height};

    return Vector2{width * scale, height * scale};
}

void UIImage::draw() {
    if (texture.id == 0) return;

    Rectangle src = source;
    if (src.width <= 0.0f || src.height <= 0.0f) {
        src = Rectangle{
            0.0f, 0.0f,
            static_cast<float>(texture.width),
            static_cast<float>(texture.height)
        };
    }

    DrawTexturePro(texture, src, screen_rect, Vector2{0.0f, 0.0f}, 0.0f, tint);
}
