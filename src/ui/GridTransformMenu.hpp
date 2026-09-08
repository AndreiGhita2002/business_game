//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_GRIDTRANSFORMMENU_HPP
#define BUSINESS_GAME_GRIDTRANSFORMMENU_HPP
#include <functional>
#include <string>

#include "ui/UINode.hpp"
#include "voxel/VoxelGrid.hpp"

#define GRID_TRANSFORM_MENU_STR "GridTransformMenu"
#define GRID_TRANSFORM_ROWS_STR "GridTransformRows"

// How the panel is laid out, in pixels. The buttons are given an explicit width
// rather than being left to measure themselves, so that they line up with the
// number rows underneath instead of each ending where its own text does.
#define GRID_TRANSFORM_GAP 4.0f
#define GRID_TRANSFORM_BUTTON_HEIGHT 26.0f
// The two bands kept for the lines naming the selected grid and saying what its
// transform is measured against. They are constants rather than the measured
// height of the text, because the rows below them are placed once in the
// constructor and the style is not known to have settled by then. Sized for the
// default style's 20 pixel font, the only one the UI uses today.
#define GRID_TRANSFORM_CAPTION_HEIGHT 22.0f
#define GRID_TRANSFORM_SUBCAPTION_HEIGHT 16.0f
// How much smaller the second line is than the first
#define GRID_TRANSFORM_SUBCAPTION_SCALE 0.7f
// Three rows for the position, three for the rotation
#define GRID_TRANSFORM_ROW_COUNT 6

class AttachMenu;
class VoxelView;

/**
 * Whether the menu is waiting on the user to pick a grid in the world.
 *
 * Only one grid is needed, so unlike the attachment menu there is a single
 * waiting stage. While it is in that stage the menu owns the world click, and
 * the other tools should be off.
 */
enum class GridTransformStage : unsigned char {
    IDLE,
    SELECT,
};

/**
 * The block of six number rows, held in a panel of its own so that all of them
 * can be shown and hidden together with the grid selection.
 *
 * It has nothing to draw: the menu behind it already carries the background,
 * and this only exists to be a thing that can be switched off in one go.
 */
class GridTransformRows final : public UINode {
public:
    // Hiding is kept separate from isEnabled, because a disabled ViewNode
    // returns before recursing to its sibling and would take every element
    // added after this one down with it. Same trick ShaderMenu uses.
    bool visible;

    std::string& get_view_type() override;

    void render() override;
    UINode* hit_test(Vector2 point) override;

    Vector2 measure() override;

    GridTransformRows(ViewNode* parent, Rectangle bounds);
};

/**
 * The panel that moves and turns a voxel grid, pinned to the right edge of the
 * window.
 *
 * Press "Select Grid" and the menu asks for a grid to be clicked in the world;
 * once one is picked, six rows appear over its local position and its rotation
 * in degrees. Each row writes straight into the grid, so the change is on the
 * screen on the next frame. The grid that is selected is outlined in the world
 * for as long as it is selected.
 *
 * The attachment buttons hang off the bottom of the same panel, because they
 * work on the same selection: the grid picked here is the **child** of an
 * attachment, the one that gets hung onto something else. See AttachMenu.
 *
 * It is a UINode for the same reason VoxelEditor is: so that the click landing
 * on its own buttons is not also treated as a click on the world.
 */
class GridTransformMenu final : public UINode {
public:
    /**
     * Run whenever the menu enters its selection stage, so that the other tools
     * that act on a world click (the voxel editor, the attachment menu) can be
     * switched off first. Set by whoever builds the UI; the menu never assumes
     * anything about what is on the other end of it.
     */
    std::function<void()> on_activate;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void draw() override;
    Vector2 measure() override;

    /**
     * True while this menu or the attachment buttons under it are waiting on a
     * click in the world, so while the pair of them own the mouse.
     */
    bool is_active() const;

    /**
     * Gives up whatever click the menu is waiting for, the attachment buttons
     * under it included. The grid that was already selected is kept, as this is
     * the user backing out of choosing a new one rather than giving up the one
     * they have.
     */
    void cancel();

    // @param voxel_view: the view holding the grids this menu moves
    GridTransformMenu(ViewNode* parent, VoxelView* voxel_view);

private:
    // The view holding the grids, and the camera the ray is cast through
    VoxelView* voxel_view;

    GridTransformStage stage;

    // The grid the rows write to, or nullptr while nothing is selected. Raw and
    // not owned: it is only valid for as long as the grid is, which is for the
    // whole run today, as nothing destroys a grid once the scene is built.
    VoxelGrid* selected_grid;

    // The rows, owned by the ViewNode child chain like every other element.
    // Kept here only so that they can be shown and hidden.
    GridTransformRows* rows;

    // The attachment buttons, under the rows and shown with them. Owned by the
    // child chain as well; kept here to hand it the selection and to switch it
    // off when the selection goes away.
    AttachMenu* attach;

    // The position rows, in the grid's local space, so relative to its parent.
    // That space is the mesher's: x is grid x, y is grid z (up) and z is grid y.
    float pos_x;
    float pos_y;
    float pos_z;

    // The rotation rows, in degrees. These are the authoritative copy rather
    // than something read back off the grid every frame: a quaternion round
    // trip is not exact, so stepping "yaw" repeatedly would slowly drift and
    // the other two angles would wander with it. They are only read off the
    // grid when a grid is newly selected.
    float pitch;
    float yaw;
    float roll;

    // The voxel under the cursor while the menu is waiting, refreshed every
    // frame. Kept as the matrix of the model that was hit and the minimum
    // corner of the voxel in that model's space, for drawing the preview.
    bool has_target;
    VoxelGrid* target_grid;
    Matrix target_matrix;
    Vector3 target_cell;

    // What the two buttons do
    void begin_select();
    void clear_selection();

    // Hangs one number row off the rows panel, at `index` down the stack
    void add_row(int index, std::string label, float* value, float step,
                 float min_value, float max_value, int decimals);

    // Copies the grid's transform into the rows, done once on selection
    void read_transform_from_grid();
    // Builds a transform out of the rows and gives it to the grid. Run by every
    // row's on_change.
    void apply_transform_to_grid();

    // Ray casts from the cursor and works out the grid a click would select
    void update_target();
    // Wireframe cube on the voxel under the cursor while waiting
    void draw_target_preview() const;
    // Bounding boxes around every model of the selected grid
    void draw_selection_highlight() const;

    // The line naming the selected grid, drawn inside the panel
    std::string caption_text() const;
    /**
     * The line under it, saying what the rows are measured against. The rows
     * are the grid's local transform, so for a grid hanging off another one
     * they are relative to that one and not to the world, which is worth saying
     * on the panel rather than leaving to be guessed.
     */
    std::string relative_to_text() const;
};

#endif //BUSINESS_GAME_GRIDTRANSFORMMENU_HPP
