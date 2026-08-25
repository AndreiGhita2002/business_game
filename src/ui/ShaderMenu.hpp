//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_SHADERMENU_HPP
#define BUSINESS_GAME_SHADERMENU_HPP
#include <functional>

#include <Shader.hpp>

#include "ui/UINode.hpp"

#define SHADER_MENU_STR "ShaderMenu"

// Key that shows and hides the menu
#define SHADER_MENU_TOGGLE_KEY KEY_F3

// Starting values for the uniforms that lighting.fs used to hold as constants.
// The menu pushes all of them when it is built, so it is the one place these
// are defined.
#define BIAS_TEXELS_DEFAULT 1.2f
#define BIAS_SLOPE_TEXELS_DEFAULT 3.0f
#define BIAS_MAX_SLOPE_DEFAULT 4.0f

/**
 * Debug panel for tuning the lighting at runtime, in the top left corner.
 *
 * One UINumberRow per value, each writing straight into the uniform (or the
 * light) it belongs to, so the effect is visible on the next frame. Hidden
 * until SHADER_MENU_TOGGLE_KEY is pressed.
 *
 * This is a debug tool. It is not meant to survive into the real UI.
 */
class ShaderMenu final : public UINode {
public:
    // Hiding is kept separate from isEnabled, because a disabled ViewNode also
    // stops its update() running, and the menu has to keep watching for its
    // toggle key while it is off screen.
    bool visible;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;
    UINode* hit_test(Vector2 point) override;

    void draw() override;
    Vector2 measure() override;

    /**
     * Adds a row under the ones already there.
     *
     * Public so that values which are not shader uniforms, such as a light's
     * shadow box, can be hung off the same panel. The number is edited in
     * place, so it must outlive the menu and must not move: taking one out of a
     * std::vector<Light> is only safe while that vector never grows again.
     */
    void add_value_row(std::string label, float* value, float step,
                       float min_value, float max_value, int decimals,
                       std::function<void()> on_change);

    // @param shader: the shader whose uniforms the bias rows write to
    ShaderMenu(ViewNode* parent, raylib::Shader* shader);

private:
    raylib::Shader* shader;

    // The values behind the bias rows, mirrored here because a shader uniform
    // cannot be read back
    float bias_texels;
    float bias_slope_texels;
    float bias_max_slope;
    float ambient_level;

    int bias_texels_loc;
    int bias_slope_texels_loc;
    int bias_max_slope_loc;
    int ambient_loc;

    int row_count;

    void push_bias() const;
    void push_ambient() const;
};

#endif //BUSINESS_GAME_SHADERMENU_HPP
