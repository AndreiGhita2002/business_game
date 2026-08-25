//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UINODE_HPP
#define BUSINESS_GAME_UINODE_HPP
#include "raylib.h"
#include "game/ViewNode.hpp"
#include "ui/UIStyle.hpp"

#define UI_NODE_STR "UINode"

class UIView;

/**
 * Which point of the parent rectangle a UINode is pinned to.
 * The order of the values matters: `value % 3` gives the column
 * (0 left, 1 centre, 2 right) and `value / 3` gives the row
 * (0 top, 1 centre, 2 bottom).
 */
enum class Anchor : unsigned char {
    TOP_LEFT = 0, TOP_CENTER, TOP_RIGHT,
    CENTER_LEFT, CENTER, CENTER_RIGHT,
    BOTTOM_LEFT, BOTTOM_CENTER, BOTTOM_RIGHT,
};

/**
 * Abstract class for UI elements.
 * Equivalent to VoxelGrid, but for UI.
 *
 * A UINode owns a rectangle that is placed relative to its parent's rectangle,
 * and is drawn by the UIView at the top of its branch of the ViewNode tree.
 * Subclasses implement draw() (and optionally measure() and the on_* callbacks)
 * rather than overriding render(), so that the layout and clipping done here
 * always applies.
 */
class UINode : public ViewNode {
public:
    /**
     * The layout request for this element.
     *  - x/y are a margin measured from the anchored edge(s), always pointing
     *    into the parent. With Anchor::BOTTOM_RIGHT, {8, 8} means 8 pixels away
     *    from the right edge and 8 pixels away from the bottom edge.
     *    For a centred axis, x/y is an extra offset in the +x/+y screen direction.
     *  - a width or height that is <= 0 means "let the element size itself",
     *    and is filled in from measure().
     */
    Rectangle bounds;
    Anchor anchor;

    /**
     * The absolute screen rectangle of this element, in pixels.
     * Recomputed every frame by UIView, before anything is drawn.
     * Elements should treat it as read only and draw themselves inside it.
     */
    Rectangle screen_rect;

    // Whether this element swallows the mouse. Set to false for elements that
    // should let clicks reach whatever is behind them (plain labels, spacers).
    bool blocks_mouse;
    // Whether children are cut off at this element's rectangle
    bool clip_children;

    // Input state, maintained by UIView. Read only for elements.
    bool hovered;
    bool pressed;

    std::string& get_view_type() override;

    void render() override;

    // --- Element hooks ---

    // Draws this element only. screen_rect is already resolved when this runs.
    virtual void draw() {}

    /**
     * The size this element would like to have, used for any axis of `bounds`
     * that was left at <= 0. The default keeps whatever `bounds` holds.
     */
    virtual Vector2 measure() { return Vector2{bounds.width, bounds.height}; }

    virtual void on_hover_enter() {}
    virtual void on_hover_exit() {}
    virtual void on_press() {}
    virtual void on_release() {}
    // Press and release both landed on this element
    virtual void on_click() {}

    // --- Framework, called by UIView ---

    // Resolves screen_rect for this element and its children.
    void resolve_layout(Rectangle parent_rect);

    /**
     * Finds the element under `point` in this subtree.
     * The last element drawn wins, so the topmost one is returned.
     */
    UINode* hit_test(Vector2 point);

    bool contains(Vector2 point) const;

    // The UIView this element belongs to, found by walking up the parents.
    UIView* get_view();
    const UIStyle& style();

    explicit UINode(ViewNode* parent, Rectangle bounds = Rectangle{}, Anchor anchor = Anchor::TOP_LEFT);
    ~UINode() override;

protected:
    UIView* view;
};

#endif //BUSINESS_GAME_UINODE_HPP
