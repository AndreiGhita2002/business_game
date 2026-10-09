//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_SETTINGSPANEL_HPP
#define BUSINESS_GAME_SETTINGSPANEL_HPP
#include <deque>
#include <functional>
#include <string>

#include "ui/UINode.hpp"

#define SETTINGS_PANEL_STR "SettingsPanel"

/**
 * A column of UINumberRows on a plate, shown and hidden by a key of its own or
 * by setting `visible` (which is what a button for it does).
 *
 * The base of the ShaderMenu and the GameSettingsMenu: everything about being
 * a panel of settings lives here, and a subclass only adds its rows and
 * whatever those rows need to push their values on.
 */
class SettingsPanel : public UINode {
public:
    // Hiding is kept separate from isEnabled, because a disabled ViewNode also
    // stops its update() running, and the panel has to keep watching for its
    // toggle key while it is off screen.
    bool visible;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;
    UINode* hit_test(Vector2 point) override;

    void draw() override;
    Vector2 measure() override;

    /**
     * Adds a row under the ones already there, editing the number in place. It
     * must outlive the panel and must not move: taking one out of a
     * std::vector is only safe while that vector never grows again.
     */
    void add_value_row(std::string label, float* value, float step,
                       float min_value, float max_value, int decimals,
                       std::function<void()> on_change);

    /**
     * A row for a whole number that the panel does not hold: `get` reads it
     * and `set` is handed a new one. That fits a value that only changes some
     * other way, such as a simulation value that changes by queueing a command.
     *
     * A UINumberRow edits a float, so the row edits one of the panel's own.
     * It is taken from `get` again whenever what `get` returns changes, not
     * every frame: a value set through a command only arrives a tick later,
     * and in between the row keeps showing what was asked for rather than
     * flicking back to the old value.
     */
    void add_int_row(std::string label, std::function<int()> get, std::function<void(int)> set,
                     int step, int min_value, int max_value);

protected:
    /** @param toggle_key: the raylib key that shows and hides the panel. */
    SettingsPanel(ViewNode* parent, int toggle_key);

private:
    struct IntRow {
        std::function<int()> get;
        std::function<void(int)> set;
        // What the row edits
        float mirror;
        // What `get` returned when the mirror last followed it
        int last_read;
    };
    // A deque, so that adding a row never moves the floats the earlier rows
    // point at
    std::deque<IntRow> int_rows;

    int toggle_key;
    int row_count;
};

#endif //BUSINESS_GAME_SETTINGSPANEL_HPP
