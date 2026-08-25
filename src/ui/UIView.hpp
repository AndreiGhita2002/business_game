//
// Created by Andrei Ghita on 25.08.2026.
//

#ifndef BUSINESS_GAME_UIVIEW_HPP
#define BUSINESS_GAME_UIVIEW_HPP
#include <vector>

#include "raylib.h"
#include "game/ViewNode.hpp"
#include "ui/UIStyle.hpp"

#define UI_VIEW_STR "UIView"

class UINode;

/**
 * UIView renders UINode children on a 2D plane.
 * Equivalent to VoxelView, but for UI elements.
 *
 * Every frame it resolves the layout of its whole subtree, routes the mouse to
 * the topmost element under the cursor, and then draws the subtree on top of
 * whatever was rendered before it.
 *
 * It draws inside the frame's BeginDrawing()/EndDrawing() block, which is
 * opened by global::mainLoop(), so a UIView must sit in the tree after the
 * views it should cover.
 */
class UIView : public ViewNode {
public:
    UIStyle style;

    // When set, the UI covers the whole window and `bounds` is ignored
    bool fill_window;
    // The area the UI is laid out in, used when fill_window is false
    Rectangle bounds;

    /**
     * True while the cursor is over a UI element that blocks the mouse.
     * The 3D views should skip their picking when this is set, so that a click
     * on a button does not also land on the world behind it.
     */
    bool mouse_consumed;

    UINode* hovered_node;
    UINode* pressed_node;
    // The element that was clicked last, kept for keyboard input later on
    UINode* focused_node;

    // The rectangle the top level elements are placed in
    Rectangle root_rect() const;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;

    // Clipping, used by UINode::render() for elements with clip_children set
    void push_clip(Rectangle rect);
    void pop_clip();

    // Drops every reference to an element that is being destroyed
    void forget(const UINode* node);

    explicit UIView(ViewNode* parent);

private:
    // raylib only has one scissor rectangle, so nested clips are tracked here
    std::vector<Rectangle> clip_stack;

    void resolve_layout();
    void dispatch_mouse();
};

#endif //BUSINESS_GAME_UIVIEW_HPP
