//
// Created by Andrei Ghita on 03.10.2025.
//

#ifndef BUSINESS_GAME_VOXELEDITOR_HPP
#define BUSINESS_GAME_VOXELEDITOR_HPP

#include <functional>

#include "ui/UINode.hpp"
#include "voxel/VoxelGrid.hpp"

#define VOXEL_EDITOR_STR "VoxelEditor"
#define VOXEL_PALETTE_CELL_STR "VoxelPaletteCell"

// The value selected_id holds while nothing is armed. Voxel id 0 is air, so it
// cannot be used for this.
#define NO_VOXEL_SELECTION (-1)

// How the palette table is laid out, in cells and pixels
#define PALETTE_COLUMNS 4
#define PALETTE_CELL_SIZE 32.0f
#define PALETTE_CELL_GAP 4.0f

class VoxelView;
class VoxelEditor;

/**
 * One cell of the editor's palette table.
 * A cell is a flat square of the colour it places. The cell holding
 * NO_VOXEL_SELECTION clears the selection instead, and is drawn with a cross.
 */
class VoxelPaletteCell final : public UINode {
public:
    // The voxel this cell places, or NO_VOXEL_SELECTION for the deselect cell
    int voxel_id;
    Color color;

    std::string& get_view_type() override;

    void draw() override;
    void on_click() override;

    VoxelPaletteCell(VoxelEditor* editor, int voxel_id, Color color, Rectangle bounds);

private:
    VoxelEditor* editor;
};

/**
 * The voxel editor: a palette of block colours, and click to place.
 *
 * Pick a colour in the table, then click in the world to put a voxel next to
 * the face that was clicked, on the first grid the ray meets. Right click
 * clears the voxel that was clicked instead. The cell that is armed is
 * outlined, and the voxel that would be placed is previewed in the world as a
 * wireframe cube.
 *
 * It is a UINode so that it is laid out by the UIView and so that clicks on the
 * palette are not also treated as clicks on the world.
 */
class VoxelEditor final : public UINode {
public:
    // The voxel id placed on click, or NO_VOXEL_SELECTION
    int selected_id;

    /**
     * Run when a colour is armed, so that the other tools that act on a world
     * click (the attachment menu, the grid transform menu) can be switched off
     * first. Set by whoever builds the UI; the editor never assumes anything
     * about what is on the other end of it.
     *
     * Not run when the selection is cleared, as that is what those tools do to
     * this one when they are armed themselves.
     */
    std::function<void()> on_select;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void draw() override;
    Vector2 measure() override;

    void select(int voxel_id);

    VoxelEditor(ViewNode* parent, VoxelView* voxel_view);

private:
    // The view holding the grids this editor writes to
    VoxelView* voxel_view;

    int cell_count;

    // What the next click would do, refreshed every frame
    bool has_target;
    VoxelGrid* target_grid;
    // The empty voxel a left click would fill
    Int3 target_pos;
    // The solid voxel a right click would clear
    Int3 remove_pos;
    // The matrix of the model that was hit, and the minimum corner of the
    // target voxel in that model's space. Kept for drawing the preview.
    Matrix target_matrix;
    Vector3 target_cell;

    // Fills the table with one cell per colour, plus the deselect cell
    void build_palette();
    // Where the cell with this index sits inside the panel
    static Rectangle cell_bounds(int index);

    // Ray casts from the cursor and works out the voxel a click would place
    void update_target();
    // Wireframe cube on the voxel a click would place
    void draw_target_preview() const;
};

#endif //BUSINESS_GAME_VOXELEDITOR_HPP
