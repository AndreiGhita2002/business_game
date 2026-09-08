//
// Created by Andrei Ghita on 29.08.2026.
//

#include "GridTransformMenu.hpp"

#include <cmath>
#include <utility>

#include <raymath.h>
#include <rlgl.h>

#include "AttachMenu.hpp"
#include "UIButton.hpp"
#include "UINumberRow.hpp"
#include "UIPrompt.hpp"
#include "UIView.hpp"
#include "game/Picking.hpp"
#include "voxel/VoxelView.hpp"

// What the user is asked to do while the menu is waiting on a grid
#define GRID_TRANSFORM_PROMPT "Select Grid   (right click or Esc to cancel)"

// The selected grid is outlined in amber, which nothing in the world is
static const Color GRID_HIGHLIGHT_COLOUR = Color{255, 200, 40, 255};

// Where the parts of the panel sit, measured from its top edge. The two buttons
// come first, then the caption, then the rows.
static float caption_top() {
    return GRID_TRANSFORM_GAP + 2.0f * (GRID_TRANSFORM_BUTTON_HEIGHT + GRID_TRANSFORM_GAP);
}

// The second caption line sits directly under the first, with no gap between
// the two, as they read as one block
static float subcaption_top() {
    return caption_top() + GRID_TRANSFORM_CAPTION_HEIGHT;
}

static float rows_top() {
    return subcaption_top() + GRID_TRANSFORM_SUBCAPTION_HEIGHT + GRID_TRANSFORM_GAP;
}

// The rows are a fixed block, so their height is known before any of them are
// built, which is what lets the attachment buttons be placed under them in the
// constructor rather than measured every frame.
static float rows_height() {
    return static_cast<float>(GRID_TRANSFORM_ROW_COUNT) * (NUMBER_ROW_HEIGHT + NUMBER_ROW_GAP)
        - NUMBER_ROW_GAP;
}

static float attach_top() {
    return rows_top() + rows_height() + GRID_TRANSFORM_GAP;
}

// A grid has a name only once it has been through a file, so one built in code
// is named by its type instead
static const std::string& grid_label(VoxelGrid* grid) {
    return grid->name.empty() ? grid->get_grid_type() : grid->name;
}

// Cuts a run of text down until it fits in `max_width`, ending it in an ellipsis
// when anything had to go. A grid name is whatever was saved in the file, so it
// is not something the panel can be sized around.
static std::string fit_text(const Font& font, const std::string& text,
                            const float size, const float spacing, const float max_width) {
    if (MeasureTextEx(font, text.c_str(), size, spacing).x <= max_width) return text;

    static const std::string ELLIPSIS = "...";
    std::string cut = text;
    while (!cut.empty()) {
        cut.pop_back();
        const std::string candidate = cut + ELLIPSIS;
        if (MeasureTextEx(font, candidate.c_str(), size, spacing).x <= max_width) return candidate;
    }
    return ELLIPSIS;
}

// ------------------------------------------------------------------ rows panel

std::string& GridTransformRows::get_view_type() {
    static std::string TYPE = GRID_TRANSFORM_ROWS_STR;
    return TYPE;
}

GridTransformRows::GridTransformRows(ViewNode* parent, const Rectangle bounds)
    : UINode(parent, bounds), visible(false)
{}

Vector2 GridTransformRows::measure() {
    const Vector2 row = UINumberRow::row_size();
    // No trailing gap: the menu adds the one below the last row itself
    return Vector2{row.x, rows_height()};
}

void GridTransformRows::render() {
    if (!isEnabled) return;

    // Only this panel and its rows are held back while hidden. The sibling
    // chain still has to be walked, or every element added after this one would
    // disappear along with the rows.
    if (visible) {
        draw();
        if (child) child->render();
    }
    if (sibling) sibling->render();
}

UINode* GridTransformRows::hit_test(const Vector2 point) {
    // Rows that are not on the screen cannot be clicked, and must not swallow
    // the world click that picks a grid
    if (!visible) return nullptr;
    return UINode::hit_test(point);
}

// ------------------------------------------------------------ transform menu

std::string& GridTransformMenu::get_view_type() {
    static std::string TYPE = GRID_TRANSFORM_MENU_STR;
    return TYPE;
}

GridTransformMenu::GridTransformMenu(ViewNode* parent, VoxelView* voxel_view)
    : UINode(parent, Rectangle{16.0f, 0.0f, 0.0f, 0.0f}, Anchor::CENTER_RIGHT),
      voxel_view(voxel_view), stage(GridTransformStage::IDLE),
      selected_grid(nullptr), rows(nullptr), attach(nullptr),
      pos_x(0.0f), pos_y(0.0f), pos_z(0.0f),
      pitch(0.0f), yaw(0.0f), roll(0.0f),
      has_target(false), target_grid(nullptr),
      target_matrix(MatrixIdentity()), target_cell(Vector3{})
{
    // Every element in the panel is as wide as a number row, so that the
    // buttons and the rows share their edges
    const float row_width = UINumberRow::row_size().x;

    add_child(std::make_unique<UIButton>(
        this, "Select Grid", [this] { begin_select(); },
        Rectangle{
            GRID_TRANSFORM_GAP, GRID_TRANSFORM_GAP,
            row_width, GRID_TRANSFORM_BUTTON_HEIGHT
        }));

    add_child(std::make_unique<UIButton>(
        this, "Deselect", [this] { clear_selection(); },
        Rectangle{
            GRID_TRANSFORM_GAP,
            GRID_TRANSFORM_GAP + GRID_TRANSFORM_BUTTON_HEIGHT + GRID_TRANSFORM_GAP,
            row_width, GRID_TRANSFORM_BUTTON_HEIGHT
        }));

    // The rows go in their own panel, which is added before them so that they
    // have a parent that is already in the tree to hang off
    auto rows_panel = std::make_unique<GridTransformRows>(
        this, Rectangle{GRID_TRANSFORM_GAP, rows_top(), row_width, 0.0f});
    rows = rows_panel.get();
    add_child(std::move(rows_panel));

    // The position is local to the parent grid, in the mesher's space, so "pos
    // y" is the one that moves the grid up and down
    add_row(0, "pos x", &pos_x, 0.5f, -1024.0f, 1024.0f, 2);
    add_row(1, "pos y", &pos_y, 0.5f, -1024.0f, 1024.0f, 2);
    add_row(2, "pos z", &pos_z, 0.5f, -1024.0f, 1024.0f, 2);

    // Fifteen degrees a step, so a quarter turn in six presses, which is the
    // turn a voxel model is usually wanted at. The range is a whole turn either
    // way rather than half, so that an angle read off a grid, which comes back
    // anywhere in -180 to 180, never lands outside its own row.
    add_row(3, "pitch", &pitch, 15.0f, -360.0f, 360.0f, 0);
    add_row(4, "yaw", &yaw, 15.0f, -360.0f, 360.0f, 0);
    add_row(5, "roll", &roll, 15.0f, -360.0f, 360.0f, 0);

    // The attachment buttons go under the rows, on the same panel, because they
    // work on the same selection. Shown and hidden with the rows for the same
    // reason: neither means anything until a grid has been picked.
    auto attach_node = std::make_unique<AttachMenu>(
        this, voxel_view, Rectangle{GRID_TRANSFORM_GAP, attach_top(), row_width, 0.0f});
    attach = attach_node.get();
    add_child(std::move(attach_node));

    // The grid selected here is the child of an attachment, the one that gets
    // hung onto something else, so the attachment menu is handed the selection
    // rather than keeping a second copy of it that could fall out of step.
    attach->get_grid = [this] { return selected_grid; };

    // Snapping moves the grid behind the rows' backs, so they are read again
    attach->on_grid_moved = [this] { read_transform_from_grid(); };

    attach->on_activate = [this] {
        // Arming the attachment is this panel arming: the tools outside are
        // told to stand down, and this menu gives up a selection of its own
        // that it was still waiting for.
        if (on_activate) on_activate();
        stage = GridTransformStage::IDLE;
    };
}

void GridTransformMenu::add_row(const int index, std::string label, float* value,
                                const float step, const float min_value,
                                const float max_value, const int decimals) {
    auto row = std::make_unique<UINumberRow>(
        rows, std::move(label), value, step, min_value, max_value, decimals,
        Rectangle{
            0.0f,
            static_cast<float>(index) * (NUMBER_ROW_HEIGHT + NUMBER_ROW_GAP),
            0.0f, 0.0f
        });
    // Every row pushes the whole transform, as a Transform is written in one go
    row->on_change = [this] { apply_transform_to_grid(); };
    rows->add_child(std::move(row));
}

bool GridTransformMenu::is_active() const {
    // The attachment buttons take world clicks through this panel, so as far as
    // anything outside is concerned the two are one tool
    return stage == GridTransformStage::SELECT
        || (attach != nullptr && attach->is_active());
}

void GridTransformMenu::begin_select() {
    // The other tools are switched off first, so that whatever on_activate does
    // to this menu cannot undo the stage that is about to be set
    if (on_activate) on_activate();

    // Picking a new grid gives up an attachment half made on the old one
    if (attach) attach->cancel();

    stage = GridTransformStage::SELECT;
    TraceLog(LOG_DEBUG, "[GRIDTRANSFORM] Waiting for a grid to be clicked");
}

void GridTransformMenu::cancel() {
    stage = GridTransformStage::IDLE;
    if (attach) attach->cancel();
}

void GridTransformMenu::clear_selection() {
    selected_grid = nullptr;
    if (rows) rows->visible = false;
    // Nothing to attach and nothing to detach without a selection, so the
    // buttons go away with the rows
    if (attach) {
        attach->cancel();
        attach->visible = false;
    }
    stage = GridTransformStage::IDLE;
    TraceLog(LOG_DEBUG, "[GRIDTRANSFORM] Selection cleared");
}

void GridTransformMenu::read_transform_from_grid() {
    if (selected_grid == nullptr) return;

    const Transform t = selected_grid->get_transform();
    pos_x = t.translation.x;
    pos_y = t.translation.y;
    pos_z = t.translation.z;

    // Radians out of raymath, as x, y, z rather than named angles
    const Vector3 euler = QuaternionToEuler(t.rotation);
    pitch = euler.x * RAD2DEG;
    yaw = euler.y * RAD2DEG;
    roll = euler.z * RAD2DEG;
}

void GridTransformMenu::apply_transform_to_grid() {
    if (selected_grid == nullptr) return;

    // The scale is read back off the grid rather than rebuilt: this menu has no
    // row for it and must not quietly reset whatever it was loaded with.
    Transform t = selected_grid->get_transform();
    t.translation = Vector3{pos_x, pos_y, pos_z};
    t.rotation = QuaternionFromEuler(pitch * DEG2RAD, yaw * DEG2RAD, roll * DEG2RAD);
    // Written as it was asked for, attached or not. The rows are the grid's
    // local transform, so what they say is always where it sits relative to its
    // parent, and an attached grid is moved and turned by them like any other.
    //
    // Nothing is snapped back onto the anchor here. snap_to_anchor() sets the
    // translation from the two connector voxels, so calling it after every
    // change would undo the position rows the moment they were touched. Being
    // attached is a parent link plus a pair of protected connector voxels, and
    // none of that depends on the two voxels sitting in the same place, so a
    // grid nudged off its anchor is still attached. "Snap To Anchor" on the
    // panel below is how it is seated again, which also matters after a
    // rotation, as turning a grid about its own origin carries the connector
    // away from the anchor.
    selected_grid->set_transform(t);
}

void GridTransformMenu::update(const float delta_time) {
    if (!isEnabled) return;

    update_target();

    if (stage == GridTransformStage::SELECT) {
        // A click that lands on this panel, or on any other UI, is not a click
        // on the world: without this the very click that pressed "Select Grid"
        // would pick a grid in the same frame. UIView has already routed the
        // mouse by the time this runs.
        const UIView* ui = get_view();
        const bool mouse_free = ui == nullptr || !ui->mouse_consumed;

        const bool given_up = IsKeyPressed(KEY_ESCAPE)
            || (mouse_free && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT));

        if (given_up) {
            TraceLog(LOG_DEBUG, "[GRIDTRANSFORM] Selection cancelled");
            cancel();
        } else if (mouse_free && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && has_target) {
            selected_grid = target_grid;
            read_transform_from_grid();
            if (rows) rows->visible = true;
            if (attach) {
                // A half made attachment was about the grid selected before
                attach->cancel();
                attach->visible = true;
            }
            stage = GridTransformStage::IDLE;

            TraceLog(LOG_INFO, "[GRIDTRANSFORM] Selected grid %s", caption_text().c_str());
        }
    }

    ViewNode::update(delta_time);
}

void GridTransformMenu::update_target() {
    has_target = false;
    target_grid = nullptr;

    if (stage != GridTransformStage::SELECT) return;

    const UIView* ui = get_view();
    if (ui != nullptr && ui->mouse_consumed) return;

    const Ray ray = GetScreenToWorldRay(GetMousePosition(), voxel_view->camera);

    VoxelRayHit hit{};
    if (!find_voxel_on_ray(ray, &voxel_view->voxel_grids, nullptr, &hit)) return;

    // Everything below is done in the model's own space, so the preview lands
    // on the right voxel for grids that are moved, turned or scaled.
    const Matrix inverse = MatrixInvert(hit.world_matrix);
    const Vector3 local_point = Vector3Transform(hit.collision.point, inverse);

    // The normal is carried into model space by moving a point along it, rather
    // than by transforming the vector, which would be wrong under scaling.
    const Vector3 local_ahead = Vector3Transform(
        Vector3Add(hit.collision.point, hit.collision.normal), inverse);
    const Vector3 local_normal = Vector3Normalize(Vector3Subtract(local_ahead, local_point));

    // The hit sits exactly on a face, so half a voxel against the normal lands
    // in the middle of the solid voxel that was pointed at.
    const Vector3 inside = Vector3Subtract(local_point, Vector3Scale(local_normal, 0.5f));

    has_target = true;
    target_grid = hit.grid;
    target_matrix = hit.world_matrix;
    target_cell = Vector3{floorf(inside.x), floorf(inside.y), floorf(inside.z)};
}

std::string GridTransformMenu::caption_text() const {
    if (selected_grid == nullptr) return "no grid selected";
    return grid_label(selected_grid);
}

std::string GridTransformMenu::relative_to_text() const {
    if (selected_grid == nullptr) return "";

    VoxelGrid* const parent_grid = selected_grid->get_parent();
    // A grid with nothing above it has its local transform straight in world
    // space, which is the one case where the rows need no qualifying
    if (parent_grid == nullptr) return "relative to the world";

    // Hanging off a grid and being attached to it are not the same thing - the
    // second adds the pair of connector voxels - and only the attached one has
    // "Snap To Anchor" to answer for, so the two are worded apart
    const std::string& parent_name = grid_label(parent_grid);
    return selected_grid->is_attached()
        ? "attached to " + parent_name
        : "relative to " + parent_name;
}

Vector2 GridTransformMenu::measure() {
    const Vector2 row = UINumberRow::row_size();

    // The panel shrinks back to the buttons and the caption when nothing is
    // selected, as the rows below are not drawn then. measure() runs every
    // frame, so a size that follows the selection costs nothing.
    const float height = (rows != nullptr && rows->visible)
        ? attach_top() + (attach != nullptr ? attach->measure().y : 0.0f) + GRID_TRANSFORM_GAP
        : subcaption_top() + GRID_TRANSFORM_SUBCAPTION_HEIGHT + GRID_TRANSFORM_GAP;

    return Vector2{row.x + 2.0f * GRID_TRANSFORM_GAP, height};
}

void GridTransformMenu::draw() {
    const UIStyle& s = style();

    // The panel behind the buttons and the rows
    DrawRectangleRec(screen_rect, s.background);
    if (s.border_thickness > 0.0f)
        DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);

    // Two lines: what the rows are writing to, and what those numbers are
    // measured against. A name comes out of a file and can be any length, so
    // both are cut to the panel rather than allowed to run off the edge of it.
    const float text_width = screen_rect.width - 2.0f * GRID_TRANSFORM_GAP;

    const std::string caption = fit_text(s.font, caption_text(), s.font_size,
                                         s.font_spacing, text_width);
    const Rectangle caption_rect = Rectangle{
        screen_rect.x + GRID_TRANSFORM_GAP,
        screen_rect.y + caption_top(),
        text_width,
        GRID_TRANSFORM_CAPTION_HEIGHT
    };
    const Vector2 caption_size = MeasureTextEx(s.font, caption.c_str(), s.font_size, s.font_spacing);
    DrawTextEx(s.font, caption.c_str(),
               anchor_in_rect(caption_size, caption_rect, Anchor::CENTER_LEFT),
               s.font_size, s.font_spacing, s.foreground);

    const float sub_size = s.font_size * GRID_TRANSFORM_SUBCAPTION_SCALE;
    const std::string relative_to = fit_text(s.font, relative_to_text(), sub_size,
                                             s.font_spacing, text_width);
    if (!relative_to.empty()) {
        const Rectangle sub_rect = Rectangle{
            screen_rect.x + GRID_TRANSFORM_GAP,
            screen_rect.y + subcaption_top(),
            text_width,
            GRID_TRANSFORM_SUBCAPTION_HEIGHT
        };
        const Vector2 sub_text_size = MeasureTextEx(s.font, relative_to.c_str(), sub_size, s.font_spacing);
        // Dimmer than the name above it, as it is the qualifier rather than the
        // thing being named
        DrawTextEx(s.font, relative_to.c_str(),
                   anchor_in_rect(sub_text_size, sub_rect, Anchor::CENTER_LEFT),
                   sub_size, s.font_spacing, Fade(s.foreground, 0.65f));
    }

    if (stage == GridTransformStage::SELECT)
        draw_screen_prompt(s, GRID_TRANSFORM_PROMPT);

    draw_selection_highlight();
    draw_target_preview();
}

void GridTransformMenu::draw_selection_highlight() const {
    if (selected_grid == nullptr) return;

    // The highlight belongs to the 3D scene, so a camera block is opened again
    // on top of the voxel pass. The depth buffer still holds the scene, so the
    // boxes are hidden by anything standing in front of them.
    BeginMode3D(voxel_view->camera); {
        for (const ModelInfo* model_info : selected_grid->get_models()) {
            if (model_info == nullptr || !model_info->do_render) continue;

            rlPushMatrix(); {
                // The same matrix the model itself was drawn with, so the box
                // sits on it whatever the grid and its parents are doing
                rlMultMatrixf(MatrixToFloat(voxel_model_matrix(selected_grid, *model_info)));

                for (int i = 0; i < model_info->model.meshCount; i++) {
                    // GetMeshBoundingBox and not GetModelBoundingBox: the
                    // latter folds model.transform in, which the matrix above
                    // has already applied, and the box would be moved twice.
                    DrawBoundingBox(GetMeshBoundingBox(model_info->model.meshes[i]),
                                    GRID_HIGHLIGHT_COLOUR);
                }
            }
            rlPopMatrix();
        }
    }
    EndMode3D();
}

void GridTransformMenu::draw_target_preview() const {
    // The stage is checked as well as the target, because the target is only
    // cleared on the next update: without this the click that picks a grid, or
    // the one that cancels, would leave the cube on the screen for a frame.
    if (!has_target || stage != GridTransformStage::SELECT) return;

    BeginMode3D(voxel_view->camera); {
        rlPushMatrix(); {
            // Drawing in the model's space keeps the cube on the voxel grid
            // even when the grid itself is moved or scaled.
            rlMultMatrixf(MatrixToFloat(target_matrix));
            DrawCubeWires(
                Vector3{target_cell.x + 0.5f, target_cell.y + 0.5f, target_cell.z + 0.5f},
                1.0f, 1.0f, 1.0f, BLACK);
        }
        rlPopMatrix();
    }
    EndMode3D();
}
