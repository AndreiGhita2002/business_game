//
// Created by Claude on 09.10.2026.
//

#include "SettingsPanel.hpp"

#include <cmath>
#include <utility>

#include "UINumberRow.hpp"

std::string& SettingsPanel::get_view_type() {
    static std::string TYPE = SETTINGS_PANEL_STR;
    return TYPE;
}

SettingsPanel::SettingsPanel(ViewNode* parent, const int toggle_key)
    : UINode(parent, Rectangle{16.0f, 16.0f, 0.0f, 0.0f}, Anchor::TOP_LEFT),
      visible(false), toggle_key(toggle_key), row_count(0)
{}

void SettingsPanel::add_value_row(std::string label, float* value, const float step,
                                  const float min_value, const float max_value, const int decimals,
                                  std::function<void()> on_change) {
    auto row = std::make_unique<UINumberRow>(
        this, std::move(label), value, step, min_value, max_value, decimals,
        Rectangle{
            NUMBER_ROW_GAP,
            NUMBER_ROW_GAP + static_cast<float>(row_count) * (NUMBER_ROW_HEIGHT + NUMBER_ROW_GAP),
            0.0f, 0.0f
        });
    row->on_change = std::move(on_change);
    add_child(std::move(row));
    row_count++;
}

void SettingsPanel::add_int_row(std::string label, std::function<int()> get, std::function<void(int)> set,
                                const int step, const int min_value, const int max_value) {
    const int now = get();
    IntRow& row = int_rows.emplace_back(IntRow{std::move(get), std::move(set), static_cast<float>(now), now});

    // Rounded rather than cut, as the float has been through additions of a
    // float step
    add_value_row(std::move(label), &row.mirror,
                  static_cast<float>(step), static_cast<float>(min_value), static_cast<float>(max_value), 0,
                  [&row] { row.set(static_cast<int>(std::lround(row.mirror))); });
}

Vector2 SettingsPanel::measure() {
    const Vector2 row = UINumberRow::row_size();
    return Vector2{
        row.x + 2.0f * NUMBER_ROW_GAP,
        static_cast<float>(row_count) * (NUMBER_ROW_HEIGHT + NUMBER_ROW_GAP) + NUMBER_ROW_GAP
    };
}

void SettingsPanel::update(const float delta_time) {
    if (!isEnabled) return;

    if (IsKeyPressed(toggle_key)) visible = !visible;

    // Follow whatever changed a value behind the panel's back (a loaded game,
    // a command from somewhere else), before the rows draw it
    for (IntRow& row : int_rows) {
        const int now = row.get();
        if (now != row.last_read) {
            row.mirror = static_cast<float>(now);
            row.last_read = now;
        }
    }

    ViewNode::update(delta_time);
}

void SettingsPanel::render() {
    if (!isEnabled) return;

    // Only the panel and its rows are held back while hidden. The sibling chain
    // still has to be walked, or every element added after this one would
    // disappear along with the panel.
    if (visible) {
        draw();
        if (child) child->render();
    }
    if (sibling) sibling->render();
}

UINode* SettingsPanel::hit_test(const Vector2 point) {
    // A hidden panel cannot be clicked, and must not swallow world clicks
    if (!visible) return nullptr;
    return UINode::hit_test(point);
}

void SettingsPanel::draw() {
    const UIStyle& s = style();

    DrawRectangleRec(screen_rect, s.background);
    if (s.border_thickness > 0.0f)
        DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);
}
