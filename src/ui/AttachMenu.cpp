//
// Created by Andrei Ghita on 29.08.2026.
//

#include "AttachMenu.hpp"

#include <cmath>
#include <memory>
#include <raymath.h>
#include <rlgl.h>

#include "UIButton.hpp"
#include "UIPrompt.hpp"
#include "UIView.hpp"
#include "game/Picking.hpp"
#include "game/Transform.hpp"
#include "voxel/VoxelView.hpp"

// The wireframe colours. The two stages of an attachment are drawn in
// different colours so that, once the connector is chosen, it is obvious which
// of the two cubes on screen is the one already settled.
#define ATTACH_HOVER_COLOUR GOLD
#define ATTACH_CONNECTOR_COLOUR SKYBLUE

// The line joining a connector to its pair, and the outline on the far end of
// it. Faint on purpose: it is there to be noticed while looking at something
// else, not to be read.
static const Color ATTACH_LINK_COLOUR = Color{102, 191, 255, 220};

std::string& AttachMenu::get_view_type() {
    static std::string TYPE = ATTACH_MENU_STR;
    return TYPE;
}

AttachMenu::AttachMenu(ViewNode* parent, VoxelView* voxel_view, const Rectangle bounds)
    : UINode(parent, bounds), visible(false),
      voxel_view(voxel_view), stage(AttachStage::IDLE),
      has_target(false), target_grid(nullptr), target_model(nullptr), target_pos(Int3{}),
      target_matrix(MatrixIdentity()), target_cell(Vector3{}),
      connector_grid(nullptr), connector_model(nullptr), connector_voxel(Int3{}),
      connector_cell(Vector3{}),
      has_links(false), link_origin(Vector3{})
{
    // The buttons take the width they are given rather than measuring
    // themselves, so that they line up with the number rows above
    const float button_width = bounds.width;

    add_child(std::make_unique<UIButton>(
        this, "Attach To", [this] { begin_attach(); },
        Rectangle{0.0f, 0.0f, button_width, ATTACH_BUTTON_HEIGHT}));

    add_child(std::make_unique<UIButton>(
        this, "Detach", [this] { detach_selected(); },
        Rectangle{
            0.0f, ATTACH_BUTTON_HEIGHT + ATTACH_MENU_GAP,
            button_width, ATTACH_BUTTON_HEIGHT
        }));

    add_child(std::make_unique<UIButton>(
        this, "Snap To Anchor", [this] { snap_selected(); },
        Rectangle{
            0.0f, 2.0f * (ATTACH_BUTTON_HEIGHT + ATTACH_MENU_GAP),
            button_width, ATTACH_BUTTON_HEIGHT
        }));
}

Vector2 AttachMenu::measure() {
    // Three buttons with a gap between each pair. No margin of its own: the
    // panel this sits in has already kept one.
    return Vector2{
        bounds.width,
        3.0f * ATTACH_BUTTON_HEIGHT + 2.0f * ATTACH_MENU_GAP,
    };
}

bool AttachMenu::is_active() const {
    return stage != AttachStage::IDLE;
}

void AttachMenu::cancel() {
    stage = AttachStage::IDLE;
    has_target = false;
    target_grid = nullptr;
    target_model = nullptr;
    connector_grid = nullptr;
    connector_model = nullptr;
}

void AttachMenu::begin_attach() {
    // Nothing to attach without a grid, and the button is not even on the
    // screen then, so this is only a guard against being driven from code
    if (get_grid == nullptr || get_grid() == nullptr) {
        TraceLog(LOG_INFO, "[ATTACH] Select a grid first, it is the one that gets attached");
        return;
    }

    // The other tools act on the same world clicks, so they are told to stand
    // down before this one starts taking them.
    if (on_activate) on_activate();
    cancel();
    stage = AttachStage::CONNECTOR;
    TraceLog(LOG_DEBUG, "[ATTACH] Waiting for the connector voxel of the selected grid");
}

void AttachMenu::detach_selected() {
    VoxelGrid* const grid = get_grid != nullptr ? get_grid() : nullptr;
    if (grid == nullptr) {
        TraceLog(LOG_INFO, "[ATTACH] Select a grid first, it is the one that comes off");
        return;
    }

    // A half made attachment is given up either way: whatever the selected grid
    // was about to be hung onto, it is not hanging off anything now.
    cancel();

    // detach() is called on the child, which is exactly what is selected
    if (grid->detach()) {
        TraceLog(LOG_DEBUG, "[ATTACH] Detached the selected grid");
        // Coming off something does not move the grid, but it does redo its
        // local transform against its new parent, so the rows are now showing
        // numbers measured against a grid that is no longer above this one
        if (on_grid_moved) on_grid_moved();
    } else {
        TraceLog(LOG_INFO, "[ATTACH] The selected grid is not attached to anything");
    }
}

void AttachMenu::snap_selected() {
    VoxelGrid* const grid = get_grid != nullptr ? get_grid() : nullptr;
    if (grid == nullptr) {
        TraceLog(LOG_INFO, "[ATTACH] Select a grid first, it is the one that gets seated");
        return;
    }

    // The counterpart of the transform rows above. They move and turn the grid
    // wherever the numbers say, which carries its connector voxel off the
    // anchor; this puts the two back on top of each other, leaving the rotation
    // and the scale exactly as they were.
    if (grid->snap_to_anchor()) {
        TraceLog(LOG_DEBUG, "[ATTACH] Snapped the selected grid onto its anchor");
        // The translation has changed under the rows, so they are told to look
        if (on_grid_moved) on_grid_moved();
    } else {
        TraceLog(LOG_INFO, "[ATTACH] The selected grid is not attached to anything to snap onto");
    }
}

void AttachMenu::update(const float delta_time) {
    if (!isEnabled) return;

    // Giving up is checked before the target is, so that the right click that
    // cancels is never also read as a selection.
    if (is_active() && (IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
        TraceLog(LOG_DEBUG, "[ATTACH] Cancelled");
        cancel();
    }

    // The grid being attached belongs to the panel above, so it can be cleared
    // or swapped for another one between two clicks. Either way what was picked
    // so far is about the grid that was selected then, and means nothing now.
    VoxelGrid* const child = get_grid != nullptr ? get_grid() : nullptr;
    if (is_active() && (child == nullptr
                        || (connector_grid != nullptr && connector_grid != child))) {
        TraceLog(LOG_INFO, "[ATTACH] The selection changed, giving up the attachment");
        cancel();
    }

    update_target();
    update_links();

    if (has_target && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        switch (stage) {
            case AttachStage::CONNECTOR:
                // The connector has to be a voxel of the grid being attached,
                // as that is the side attach_to() reads it in the space of
                if (target_grid != child) {
                    TraceLog(LOG_INFO, "[ATTACH] Click a voxel of the selected grid, that is the one being attached");
                    break;
                }
                // Only remembered here; nothing is written until the anchor is
                // picked as well.
                connector_grid = target_grid;
                connector_model = target_model;
                connector_voxel = target_pos;
                connector_cell = target_cell;
                stage = AttachStage::ANCHOR;
                TraceLog(LOG_DEBUG, "[ATTACH] Connector voxel %d,%d,%d",
                         connector_voxel.x, connector_voxel.y, connector_voxel.z);
                break;

            case AttachStage::ANCHOR:
                if (target_grid == child) {
                    // attach_to() would refuse this as a cycle, but saying so
                    // here is clearer than letting it log about ancestors.
                    TraceLog(LOG_INFO, "[ATTACH] A grid cannot be attached to itself, pick another grid");
                    break;
                }
                // The call is made on the child: it is the grid that gains a
                // parent and an attachment, and attach_to() refuses a cycle, a
                // connector that is not solid and so on, logging why.
                if (child->attach_to(target_grid, target_pos, connector_voxel)) {
                    TraceLog(LOG_DEBUG, "[ATTACH] Attached to anchor %d,%d,%d",
                             target_pos.x, target_pos.y, target_pos.z);
                    cancel();
                    // attach_to() reparents the grid and snaps it onto the
                    // anchor, so its local transform is both against a new
                    // parent and at a new place. The rows above are showing
                    // neither until they are told to look again.
                    if (on_grid_moved) on_grid_moved();
                } else {
                    // Left in this stage so that another voxel can be tried
                    // without pressing the button again.
                    TraceLog(LOG_INFO, "[ATTACH] Refused anchor %d,%d,%d, pick another voxel",
                             target_pos.x, target_pos.y, target_pos.z);
                }
                break;

            case AttachStage::IDLE:
                break;
        }
    }

    ViewNode::update(delta_time);
}

void AttachMenu::update_target() {
    has_target = false;

    // Whenever the panel is on the screen, not only while it is waiting for a
    // click: the attachment links under the cursor are shown for as long as a
    // grid is selected, and they need this same ray cast. Nothing acts on the
    // target while the menu is idle, see the stage switch in update().
    if (!visible) return;

    // A click that lands on this panel, or on any other UI, is not a click on
    // the world. UIView has already routed the mouse by the time this runs, so
    // this is what stops the press on "Attach To" from also picking a voxel in
    // the very same frame.
    const UIView* ui = get_view();
    if (ui != nullptr && ui->mouse_consumed) return;

    const Ray ray = GetScreenToWorldRay(GetMousePosition(), voxel_view->camera);

    VoxelRayHit hit{};
    if (!find_voxel_on_ray(ray, &voxel_view->voxel_grids, nullptr, &hit)) return;

    // Everything below is done in the model's own space, so it holds for grids
    // that are moved, turned or scaled.
    const Matrix inverse = MatrixInvert(hit.world_matrix);
    const Vector3 local_point = Vector3Transform(hit.collision.point, inverse);

    // The normal is carried into model space by moving a point along it, rather
    // than by transforming the vector, which would be wrong under scaling.
    const Vector3 local_ahead = Vector3Transform(
        Vector3Add(hit.collision.point, hit.collision.normal), inverse);
    const Vector3 local_normal = Vector3Normalize(Vector3Subtract(local_ahead, local_point));

    // The hit sits exactly on a face, so half a voxel against the normal lands
    // in the middle of the solid voxel that was clicked. A connector has to be
    // a solid voxel, so that is the one wanted here, never the empty cell in
    // front of it.
    const Vector3 solid_point = Vector3Subtract(local_point, Vector3Scale(local_normal, 0.5f));

    Int3 grid_pos{};
    if (!hit.grid->model_to_grid(hit.model, solid_point, &grid_pos)) return;

    has_target = true;
    target_grid = hit.grid;
    target_model = hit.model;
    target_pos = grid_pos;
    target_matrix = hit.world_matrix;
    target_cell = Vector3{
        floorf(solid_point.x),
        floorf(solid_point.y),
        floorf(solid_point.z),
    };
}

void AttachMenu::update_links() {
    links.clear();
    has_links = false;

    if (!has_target || target_grid == nullptr) return;

    // The voxel is this grid's own connector, so the far end is the anchor
    // voxel it is held by on its parent
    const Attachment* own = target_grid->get_attachment();
    if (own != nullptr && own->anchor != nullptr && own->local_voxel == target_pos)
        links.emplace_back(AttachmentLink{own->anchor, own->anchor_voxel});

    // And the other direction. What is attached *to* a grid is read off its
    // children rather than stored on it, the same way is_connector_voxel() does
    // it, so a voxel carrying several grids gives a line to each of them.
    for (VoxelGrid* child : target_grid->get_children()) {
        if (child == nullptr) continue;
        const Attachment* attached = child->get_attachment();
        if (attached == nullptr || attached->anchor != target_grid) continue;
        if (!(attached->anchor_voxel == target_pos)) continue;
        links.emplace_back(AttachmentLink{child, attached->local_voxel});
    }

    if (links.empty()) return;

    has_links = true;
    // voxel_centre_local puts a voxel in its grid's own space, which is the
    // space the grid's transform is measured in, so the world transform carries
    // it the rest of the way
    link_origin = apply_transform_trans(VoxelGrid::voxel_centre_local(target_pos),
                                        target_grid->get_world_transform());
}

void AttachMenu::render() {
    if (!isEnabled) return;

    // Only this panel and its buttons are held back while hidden. The sibling
    // chain still has to be walked, or every element added after this one would
    // disappear along with it.
    if (visible) {
        draw();
        if (child) child->render();
    }
    if (sibling) sibling->render();
}

UINode* AttachMenu::hit_test(const Vector2 point) {
    // Buttons that are not on the screen cannot be clicked, and must not
    // swallow a click meant for the world
    if (!visible) return nullptr;
    return UINode::hit_test(point);
}

void AttachMenu::draw() {
    // No background of its own: the transform panel this sits in has already
    // drawn one behind the whole column.
    draw_links();
    draw_previews();

    if (is_active()) draw_screen_prompt(style(), prompt_text());
}

void AttachMenu::draw_previews() {
    if (!is_active()) return;

    const bool show_connector = stage == AttachStage::ANCHOR
        && connector_grid != nullptr && connector_model != nullptr;
    if (!has_target && !show_connector) return;

    // The previews belong to the 3D scene, so a camera block is opened again on
    // top of the voxel pass. The depth buffer still holds the scene, so a cube
    // is hidden by anything standing in front of it. Both cubes go in the one
    // block, as opening a second would be a second pass for no reason.
    BeginMode3D(voxel_view->camera); {
        if (show_connector) {
            // Worked out afresh: the grid being attached is free to be moved by
            // the rows above between the two clicks.
            const Matrix connector_matrix = voxel_model_matrix(connector_grid, *connector_model);
            rlPushMatrix(); {
                rlMultMatrixf(MatrixToFloat(connector_matrix));
                DrawCubeWires(
                    Vector3{connector_cell.x + 0.5f, connector_cell.y + 0.5f, connector_cell.z + 0.5f},
                    1.0f, 1.0f, 1.0f, ATTACH_CONNECTOR_COLOUR);
            }
            rlPopMatrix();
        }

        if (has_target) {
            rlPushMatrix(); {
                // Drawing in the model's space keeps the cube on the voxel grid
                // even when the grid itself is moved or scaled.
                rlMultMatrixf(MatrixToFloat(target_matrix));
                DrawCubeWires(
                    Vector3{target_cell.x + 0.5f, target_cell.y + 0.5f, target_cell.z + 0.5f},
                    1.0f, 1.0f, 1.0f, ATTACH_HOVER_COLOUR);
            }
            rlPopMatrix();
        }
    }
    EndMode3D();
}

void AttachMenu::draw_links() const {
    if (!has_links) return;

    BeginMode3D(voxel_view->camera); {
        // Both ends of an attachment sit inside solid voxels, and after a snap
        // they sit in the same place, so a line drawn with the depth test on
        // would almost never be seen. rlgl batches its geometry, so the batch
        // has to be flushed before and after the depth test is touched, or the
        // change lands on whatever is drawn next instead of on this.
        rlDrawRenderBatchActive();
        rlDisableDepthTest();

        for (const AttachmentLink& link : links) {
            if (link.grid == nullptr) continue;

            const Transform world = link.grid->get_world_transform();
            const Vector3 far_end = apply_transform_trans(
                VoxelGrid::voxel_centre_local(link.voxel), world);

            DrawLine3D(link_origin, far_end, ATTACH_LINK_COLOUR);

            // The far voxel is outlined in its own grid's space rather than as
            // a cube in the world, so it stays on the voxel for a grid that is
            // turned or scaled. A line ending in the middle of a solid mass
            // says very little on its own.
            rlPushMatrix(); {
                rlMultMatrixf(MatrixToFloat(transform_to_matrix(world)));
                DrawCubeWires(VoxelGrid::voxel_centre_local(link.voxel),
                              1.0f, 1.0f, 1.0f, ATTACH_LINK_COLOUR);
            }
            rlPopMatrix();
        }

        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }
    EndMode3D();
}

const std::string& AttachMenu::prompt_text() const {
    // Static strings rather than ones built per frame: the text never depends
    // on anything but the stage.
    static const std::string CONNECTOR_TEXT =
        "Select Connector Voxel on the selected grid   (right click or Esc to cancel)";
    static const std::string ANCHOR_TEXT =
        "Select Anchor Voxel on the grid to attach to   (right click or Esc to cancel)";
    static const std::string NO_TEXT;

    switch (stage) {
        case AttachStage::CONNECTOR: return CONNECTOR_TEXT;
        case AttachStage::ANCHOR: return ANCHOR_TEXT;
        case AttachStage::IDLE: break;
    }
    return NO_TEXT;
}
