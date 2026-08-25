//
// Created by Andrei Ghita on 25.08.2026.
//

#include "UIView.hpp"

#include "UINode.hpp"

std::string& UIView::get_view_type() {
    static std::string TYPE = UI_VIEW_STR;
    return TYPE;
}

UIView::UIView(ViewNode* parent)
    : ViewNode(parent), style(default_ui_style()),
      fill_window(true), bounds(Rectangle{}), mouse_consumed(false),
      hovered_node(nullptr), pressed_node(nullptr), focused_node(nullptr)
{}

Rectangle UIView::root_rect() const {
    if (!fill_window) return bounds;
    // Read every frame, so that the UI follows a resized window
    return Rectangle{
        0.0f, 0.0f,
        static_cast<float>(GetScreenWidth()),
        static_cast<float>(GetScreenHeight())
    };
}

void UIView::update(const float delta_time) {
    // The layout has to be up to date before the mouse is routed, and both have
    // to happen before anything is drawn.
    resolve_layout();
    dispatch_mouse();

    ViewNode::update(delta_time);
}

void UIView::render() {
    if (!isEnabled) return;

    // Same painter's order as UINode::render(): the subtree first, siblings after
    if (child) {
        if (!fill_window) push_clip(bounds);
        child->render();
        if (!fill_window) pop_clip();
    }
    if (sibling) sibling->render();
}

void UIView::resolve_layout() {
    const Rectangle root = root_rect();
    for (ViewNode* node = child.get(); node != nullptr; node = node->sibling.get()) {
        if (auto* ui_child = dynamic_cast<UINode*>(node))
            ui_child->resolve_layout(root);
    }
}

void UIView::dispatch_mouse() {
    const Vector2 mouse = GetMousePosition();

    // The topmost element under the cursor, or nullptr if the cursor is over the world
    UINode* hit = nullptr;
    for (ViewNode* node = child.get(); node != nullptr; node = node->sibling.get()) {
        if (auto* ui_child = dynamic_cast<UINode*>(node)) {
            if (UINode* child_hit = ui_child->hit_test(mouse)) hit = child_hit;
        }
    }

    // A drag that started on an element keeps the mouse until it is released
    mouse_consumed = hit != nullptr || pressed_node != nullptr;

    // Hovering
    if (hit != hovered_node) {
        if (hovered_node) {
            hovered_node->hovered = false;
            hovered_node->on_hover_exit();
        }
        hovered_node = hit;
        if (hovered_node) {
            hovered_node->hovered = true;
            hovered_node->on_hover_enter();
        }
    }

    // Pressing
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        focused_node = hit;
        pressed_node = hit;
        if (pressed_node) {
            pressed_node->pressed = true;
            pressed_node->on_press();
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && pressed_node) {
        pressed_node->pressed = false;
        pressed_node->on_release();
        // A click only counts if the button went down and up on the same element
        if (pressed_node == hit) pressed_node->on_click();
        pressed_node = nullptr;
    }
}

void UIView::push_clip(const Rectangle rect) {
    // Nested clips are intersected by hand, as raylib keeps a single scissor
    // rectangle. An empty intersection hides everything, which is what we want.
    const Rectangle clip = clip_stack.empty() ? rect : GetCollisionRec(clip_stack.back(), rect);
    clip_stack.push_back(clip);

    BeginScissorMode(
        static_cast<int>(clip.x), static_cast<int>(clip.y),
        static_cast<int>(clip.width), static_cast<int>(clip.height));
}

void UIView::pop_clip() {
    if (clip_stack.empty()) return;
    clip_stack.pop_back();

    if (clip_stack.empty()) {
        EndScissorMode();
    } else {
        // Back to the clip of the enclosing element
        const Rectangle& clip = clip_stack.back();
        BeginScissorMode(
            static_cast<int>(clip.x), static_cast<int>(clip.y),
            static_cast<int>(clip.width), static_cast<int>(clip.height));
    }
}

void UIView::forget(const UINode* node) {
    if (hovered_node == node) hovered_node = nullptr;
    if (pressed_node == node) pressed_node = nullptr;
    if (focused_node == node) focused_node = nullptr;
}
