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

// How much of the baked ambient occlusion comes off direct light as well as
// ambient. The shader holds no default of its own, so this is the one place it
// is defined.
#define AO_DIRECT_DEFAULT 0.5f

/**
 * Debug panel for tuning the lighting at runtime, in the top left corner.
 *
 * One UINumberRow per value, each writing straight into the uniform (or the
 * light) it belongs to, so the effect is visible on the next frame. Hidden
 * until SHADER_MENU_TOGGLE_KEY is pressed.
 *
 * The shadow bias rows that used to be here are gone with the shadow map:
 * shadows are traced through the world's voxels now and have nothing to tune.
 * What is left is the ambient level, the step count view, and whatever else is
 * hung off the panel with add_value_row().
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
     * angle in the sky, can be hung off the same panel. The number is edited in
     * place, so it must outlive the menu and must not move: taking one out of a
     * std::vector<Light> is only safe while that vector never grows again.
     */
    void add_value_row(std::string label, float* value, float step,
                       float min_value, float max_value, int decimals,
                       std::function<void()> on_change);

    // @param shader: the shader whose uniforms the rows write to
    ShaderMenu(ViewNode* parent, raylib::Shader* shader);

private:
    raylib::Shader* shader;

    // The values behind the rows, mirrored here because a shader uniform
    // cannot be read back
    float ambient_level;
    // How much of the mesh's baked ambient occlusion is taken off direct light
    float ao_direct;
    // Colours every fragment by how far its shadow ray travelled, for finding
    // the ones that cost the most. A number rather than a flag, as that is what
    // a UINumberRow edits: anything but 0 is on.
    float shadow_steps_view;

    int ambient_loc;
    int ao_direct_loc;
    int debug_shadow_steps_loc;

    int row_count;

    void push_ambient() const;
    void push_ao_direct() const;
    void push_shadow_steps_view() const;
};

#endif //BUSINESS_GAME_SHADERMENU_HPP
