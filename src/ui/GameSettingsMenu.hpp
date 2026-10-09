//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_GAMESETTINGSMENU_HPP
#define BUSINESS_GAME_GAMESETTINGSMENU_HPP
#include "ui/SettingsPanel.hpp"

#define GAME_SETTINGS_MENU_STR "GameSettingsMenu"

// Key that shows and hides the menu
#define GAME_SETTINGS_MENU_TOGGLE_KEY KEY_F4

/**
 * Panel of the game's settings, hidden until GAME_SETTINGS_MENU_TOGGLE_KEY or
 * its button. The ShaderMenu's sibling, for the world rather than the
 * lighting. It has no rows of its own: everything on it is hung off it from
 * main.cpp, so the menu knows nothing about what it edits. The water level,
 * which is the simulation's and so changed by a command, is the first.
 */
class GameSettingsMenu final : public SettingsPanel {
public:
    std::string& get_view_type() override;

    explicit GameSettingsMenu(ViewNode* parent);
};

#endif //BUSINESS_GAME_GAMESETTINGSMENU_HPP
