//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_ATTACHMENU_HPP
#define BUSINESS_GAME_ATTACHMENU_HPP
#include <functional>
#include <string>
#include <vector>

#include "ui/UINode.hpp"
#include "voxel/VoxelGrid.hpp"

#define ATTACH_MENU_STR "AttachMenu"

// How the panel's buttons are laid out, in pixels. They are as wide as whatever
// bounds the owner gives the panel, so that they line up with the rows above.
#define ATTACH_MENU_GAP 4.0f
#define ATTACH_BUTTON_HEIGHT 26.0f

class VoxelView;

/**
 * The far end of an attachment that meets the voxel under the cursor: the grid
 * the other connector belongs to, and that connector in its own coordinates.
 *
 * One voxel can be several of these at once - a grid's own connector is also
 * free to be the anchor of any number of grids attached to it - so they are
 * collected rather than reduced to one.
 */
struct AttachmentLink {
    VoxelGrid* grid;
    Int3 voxel;
};

/**
 * Which voxel, if any, the menu is waiting for.
 *
 * An attachment is two voxels on two different grids, and the grid being
 * attached is already known - it is whatever the menu above has selected - so
 * the connector on it is asked for first, and the anchor it goes onto second.
 */
enum class AttachStage : unsigned char {
    IDLE,
    CONNECTOR,
    ANCHOR,
};

/**
 * The two buttons that drive VoxelGrid::attach_to and VoxelGrid::detach, sat
 * under the grid transform controls.
 *
 * It has no grid of its own to work on: the grid is the one GridTransformMenu
 * has selected, read through `get_grid`, and it is always the **child** of the
 * attachment - the thing being hung onto something else. So the flow reads the
 * way the tree does: pick the grid to move, press "Attach To", say which of its
 * voxels is the connector, then say which voxel of the other grid to hang it
 * off.
 *
 * "Detach" and "Snap To Anchor" need no clicking at all, as both act on the
 * child and the child is the grid already selected. Snapping is the counterpart
 * of the transform rows above: they move an attached grid freely, off its
 * anchor if that is what the numbers say, and this puts the connector voxel
 * back on the anchor voxel afterwards.
 *
 * It is a UINode for the same reason VoxelEditor is: so that the click landing
 * on its own buttons is not also treated as a click on the world.
 */
class AttachMenu final : public UINode {
public:
    // Hidden until a grid is selected, as neither button means anything
    // without one. Kept separate from isEnabled, because a disabled ViewNode
    // returns before recursing to its sibling and would take every element
    // added after this one down with it. Same trick ShaderMenu uses.
    bool visible;

    /**
     * The grid to attach, which is the child of the attachment. Supplied by
     * whoever builds the menu rather than held here, so that it is always the
     * same selection the panel above is showing, with no second copy to keep
     * in step. Returning nullptr means nothing is selected.
     */
    std::function<VoxelGrid*()> get_grid;

    /**
     * Run whenever the menu enters a selection stage, so that the other tools
     * that act on a world click (the voxel editor, the grid transform menu
     * above) can be switched off first. Set by whoever builds the UI; the menu
     * never assumes anything about what is on the other end of it.
     */
    std::function<void()> on_activate;

    /**
     * Run after the menu has moved the selected grid itself, so that the rows
     * showing its transform can be read again rather than going stale.
     *
     * All three buttons move the grid: attaching snaps it onto its anchor,
     * detaching redoes its local transform against whatever it hangs off now,
     * and snapping is nothing but a move.
     */
    std::function<void()> on_grid_moved;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;
    UINode* hit_test(Vector2 point) override;

    void draw() override;
    Vector2 measure() override;

    /** True while the menu is waiting on a voxel, so while it owns the click. */
    bool is_active() const;

    /**
     * Back to the idle stage, forgetting a half made selection. Also the one
     * place the remembered grid pointers are dropped, see below.
     */
    void cancel();

    // @param voxel_view: the view holding the grids this menu attaches
    // @param bounds: where the panel sits inside its parent. The width has to
    //                be given, as the buttons are cut from it.
    AttachMenu(ViewNode* parent, VoxelView* voxel_view, Rectangle bounds);

private:
    // The view holding the grids, and the camera the ray is cast through
    VoxelView* voxel_view;

    AttachStage stage;

    // What the next click would act on, refreshed every frame while active.
    // This is the solid voxel that was hit, not the empty one next to it.
    bool has_target;
    VoxelGrid* target_grid;
    // The model of that grid the ray landed on, kept so that a voxel picked now
    // can have its matrix worked out again on a later frame
    ModelInfo* target_model;
    Int3 target_pos;
    // The matrix of the model that was hit, and the minimum corner of the
    // target voxel in that model's space. Kept for drawing the preview.
    Matrix target_matrix;
    Vector3 target_cell;

    // The connector picked in the CONNECTOR stage, held until the anchor is
    // picked. The grid is the selected one at the time of that click, kept so
    // that a selection changed in between is noticed rather than attached by
    // mistake. The two pointers are raw and are not owned: they are only valid
    // for as long as the grid is, and cancel() is what clears them, so anything
    // that destroys a grid should cancel the menu.
    VoxelGrid* connector_grid;
    ModelInfo* connector_model;
    Int3 connector_voxel;
    // The minimum corner of the connector voxel, in its model's space
    Vector3 connector_cell;

    // The voxel under the cursor when it is holding an attachment together:
    // where it is in the world, and the connector on the other side of each
    // attachment that meets it. Rebuilt every frame, so nothing here outlives
    // the grid it points at.
    bool has_links;
    Vector3 link_origin;
    std::vector<AttachmentLink> links;

    // What the three buttons do
    void begin_attach();
    void detach_selected();
    void snap_selected();

    // Ray casts from the cursor and works out the voxel a click would act on
    void update_target();
    // Works out which attachments meet the voxel under the cursor
    void update_links();
    // Wireframe cubes on the hovered voxel and on the connector already chosen
    void draw_previews();
    /**
     * A faint line from the connector under the cursor to the one it is held
     * by, or to each of the ones it holds. Both are usually buried inside the
     * two grids, so they are drawn through whatever is in front of them.
     */
    void draw_links() const;
    // The line of text shown across the middle of the window while active
    const std::string& prompt_text() const;
};

#endif //BUSINESS_GAME_ATTACHMENU_HPP
