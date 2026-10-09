//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_SHADERMENU_HPP
#define BUSINESS_GAME_SHADERMENU_HPP
#include <Shader.hpp>

#include "ui/SettingsPanel.hpp"

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
 * until SHADER_MENU_TOGGLE_KEY is pressed. Being a panel of rows is
 * SettingsPanel's job; this class only owns the uniforms.
 *
 * The shadow bias rows that used to be here are gone with the shadow map:
 * shadows are traced through the world's voxels now and have nothing to tune.
 * What is left is the ambient level, the step count view, and whatever else is
 * hung off the panel with add_value_row() (see SettingsPanel).
 *
 * This is a debug tool. It is not meant to survive into the real UI.
 */
class ShaderMenu final : public SettingsPanel {
public:
    std::string& get_view_type() override;

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

    void push_ambient() const;
    void push_ao_direct() const;
    void push_shadow_steps_view() const;
};

#endif //BUSINESS_GAME_SHADERMENU_HPP
