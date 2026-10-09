//
// Created by Claude on 09.10.2026.
//

#include "GameSettingsMenu.hpp"

std::string& GameSettingsMenu::get_view_type() {
    static std::string TYPE = GAME_SETTINGS_MENU_STR;
    return TYPE;
}

GameSettingsMenu::GameSettingsMenu(ViewNode* parent)
    : SettingsPanel(parent, GAME_SETTINGS_MENU_TOGGLE_KEY)
{}
