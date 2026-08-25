//
// Created by Andrei Ghita on 03.10.2025.
//

#include "VoxelEditor.hpp"

#include <cmath>
#include <raymath.h>
#include <rlgl.h>

#include "UIView.hpp"
#include "game/main.hpp"
#include "voxel/VoxelView.hpp"

// ---------------------------------------------------------------- palette cell

std::string& VoxelPaletteCell::get_view_type() {
    static std::string TYPE = VOXEL_PALETTE_CELL_STR;
    return TYPE;
}

VoxelPaletteCell::VoxelPaletteCell(VoxelEditor* editor, const int voxel_id,
                                   const Color color, const Rectangle bounds)
    : UINode(editor, bounds), voxel_id(voxel_id), color(color), editor(editor)
{}

void VoxelPaletteCell::draw() {
    const UIStyle& s = style();

    if (voxel_id == NO_VOXEL_SELECTION) {
        // The deselect cell has no colour of its own, so it is drawn as a cross
        DrawRectangleRec(screen_rect, s.accent);
        const float inset = s.padding;
        DrawLineEx(
            Vector2{screen_rect.x + inset, screen_rect.y + inset},
            Vector2{screen_rect.x + screen_rect.width - inset, screen_rect.y + screen_rect.height - inset},
            2.0f, s.foreground);
        DrawLineEx(
            Vector2{screen_rect.x + screen_rect.width - inset, screen_rect.y + inset},
            Vector2{screen_rect.x + inset, screen_rect.y + screen_rect.height - inset},
            2.0f, s.foreground);
    } else {
        DrawRectangleRec(screen_rect, color);
    }

    // The armed cell is outlined, so it stands out from a plain hover
    if (editor->selected_id == voxel_id)
        DrawRectangleLinesEx(screen_rect, 3.0f, s.foreground);
    else if (hovered)
        DrawRectangleLinesEx(screen_rect, 1.0f, s.foreground);
}

void VoxelPaletteCell::on_click() {
    editor->select(voxel_id);
}

// --------------------------------------------------------------- voxel editor

std::string& VoxelEditor::get_view_type() {
    static std::string TYPE = VOXEL_EDITOR_STR;
    return TYPE;
}

VoxelEditor::VoxelEditor(ViewNode* parent, VoxelView* voxel_view)
    : UINode(parent, Rectangle{16.0f, 16.0f, 0.0f, 0.0f}, Anchor::BOTTOM_RIGHT),
      selected_id(NO_VOXEL_SELECTION), voxel_view(voxel_view), cell_count(0),
      has_target(false), target_grid(nullptr), target_pos(Int3{}), remove_pos(Int3{}),
      target_matrix(MatrixIdentity()), target_cell(Vector3{})
{
    build_palette();
}

void VoxelEditor::select(const int voxel_id) {
    selected_id = voxel_id;
    TraceLog(LOG_DEBUG, "[EDITOR] Selected voxel id: %d", voxel_id);
}

Rectangle VoxelEditor::cell_bounds(const int index) {
    const auto column = static_cast<float>(index % PALETTE_COLUMNS);
    const auto row = static_cast<float>(index / PALETTE_COLUMNS);
    return Rectangle{
        PALETTE_CELL_GAP + column * (PALETTE_CELL_SIZE + PALETTE_CELL_GAP),
        PALETTE_CELL_GAP + row * (PALETTE_CELL_SIZE + PALETTE_CELL_GAP),
        PALETTE_CELL_SIZE,
        PALETTE_CELL_SIZE,
    };
}

void VoxelEditor::build_palette() {
    // The palette is the colour map the grids are meshed with, so a cell can
    // only offer a colour the mesher is able to draw.
    const VoxelColourMap colours = voxel_view->game_map->voxel_colours;

    int index = 0;
    for (const auto& [id, color] : *colours) {
        if (id == 0) continue;  // air is not something to place
        add_child(std::make_unique<VoxelPaletteCell>(this, id, color, cell_bounds(index)));
        index++;
    }

    // The table always ends with the deselect cell
    add_child(std::make_unique<VoxelPaletteCell>(this, NO_VOXEL_SELECTION, BLANK, cell_bounds(index)));
    cell_count = index + 1;
}

Vector2 VoxelEditor::measure() {
    const int columns = cell_count < PALETTE_COLUMNS ? cell_count : PALETTE_COLUMNS;
    const int rows = (cell_count + PALETTE_COLUMNS - 1) / PALETTE_COLUMNS;

    return Vector2{
        static_cast<float>(columns) * (PALETTE_CELL_SIZE + PALETTE_CELL_GAP) + PALETTE_CELL_GAP,
        static_cast<float>(rows) * (PALETTE_CELL_SIZE + PALETTE_CELL_GAP) + PALETTE_CELL_GAP,
    };
}

void VoxelEditor::update(const float delta_time) {
    if (!isEnabled) return;

    update_target();

    if (has_target && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (target_grid->set_voxel(target_pos, static_cast<VoxelID>(selected_id))) {
            TraceLog(LOG_DEBUG, "[EDITOR] Placed voxel %d at %d,%d,%d",
                     selected_id, target_pos.x, target_pos.y, target_pos.z);
        } else {
            TraceLog(LOG_DEBUG, "[EDITOR] Cannot place outside the grid: %d,%d,%d",
                     target_pos.x, target_pos.y, target_pos.z);
        }
    }

    if (has_target && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        //TODO (design) decide what a grid should do once its last voxel is gone.
        // An emptied grid meshes to a model with no meshes at all, so no ray can
        // ever hit it again: it is invisible and cannot be built on, but it is
        // still in VoxelView::voxel_grids holding its transform. Either drop it
        // from the view, or keep it around with something clickable standing in
        // for it. This does not apply to VoxelMap, whose chunks are never on
        // their own.
        if (target_grid->set_voxel(remove_pos, 0)) {
            TraceLog(LOG_DEBUG, "[EDITOR] Removed voxel at %d,%d,%d",
                     remove_pos.x, remove_pos.y, remove_pos.z);
        }
    }

    ViewNode::update(delta_time);
}

void VoxelEditor::update_target() {
    has_target = false;

    if (selected_id == NO_VOXEL_SELECTION) return;

    // A click that lands on the palette, or on any other UI, is not a click on
    // the world. UIView has already routed the mouse by the time this runs.
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

    // The hit sits exactly on a face, so half a voxel along the normal lands in
    // the middle of the empty cell next to it, and half a voxel against the
    // normal lands in the middle of the solid one that was clicked.
    const Vector3 target_point = Vector3Add(local_point, Vector3Scale(local_normal, 0.5f));
    const Vector3 remove_point = Vector3Subtract(local_point, Vector3Scale(local_normal, 0.5f));

    Int3 grid_pos{};
    Int3 hit_pos{};
    if (!hit.grid->model_to_grid(hit.model, target_point, &grid_pos)) return;
    if (!hit.grid->model_to_grid(hit.model, remove_point, &hit_pos)) return;

    has_target = true;
    target_grid = hit.grid;
    target_pos = grid_pos;
    remove_pos = hit_pos;
    target_matrix = hit.world_matrix;
    target_cell = Vector3{
        floorf(target_point.x),
        floorf(target_point.y),
        floorf(target_point.z),
    };
}

void VoxelEditor::draw() {
    const UIStyle& s = style();

    // The panel behind the table
    DrawRectangleRec(screen_rect, s.background);
    if (s.border_thickness > 0.0f)
        DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);

    draw_target_preview();
}

void VoxelEditor::draw_target_preview() const {
    if (!has_target) return;

    // The preview belongs to the 3D scene, so a camera block is opened again on
    // top of the voxel pass. The depth buffer still holds the scene, so the cube
    // is hidden by anything standing in front of it.
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
