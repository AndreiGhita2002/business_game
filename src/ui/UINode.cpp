//
// Created by Andrei Ghita on 25.08.2026.
//

#include "UINode.hpp"

#include "UIView.hpp"

std::string& UINode::get_view_type() {
    static std::string TYPE = UI_NODE_STR;
    return TYPE;
}

UINode::UINode(ViewNode* parent, const Rectangle bounds, const Anchor anchor)
    : ViewNode(parent), bounds(bounds), anchor(anchor), screen_rect(bounds),
      blocks_mouse(true), clip_children(false),
      hovered(false), pressed(false), view(nullptr)
{
    get_view();
}

UINode::~UINode() {
    // The view keeps raw pointers to the hovered/pressed/focused elements,
    // so it has to be told when one of them goes away.
    // Only the cached pointer is used here: walking the parents of a
    // half destroyed tree is not safe.
    if (view) view->forget(this);
}

UIView* UINode::get_view() {
    if (view) return view;

    for (ViewNode* node = parent; node != nullptr; node = node->parent) {
        // dynamic_cast rather than get_view_type(), because every element
        // overrides get_view_type() with its own name.
        if (auto* found = dynamic_cast<UIView*>(node)) {
            view = found;
            break;
        }
    }
    return view;
}

const UIStyle& UINode::style() {
    if (const UIView* v = get_view()) return v->style;

    // Detached elements still need something to draw with
    static UIStyle fallback = default_ui_style();
    return fallback;
}

void UINode::resolve_layout(const Rectangle parent_rect) {
    Vector2 size = Vector2{bounds.width, bounds.height};
    // An axis left at <= 0 is handed over to the element itself
    if (size.x <= 0.0f || size.y <= 0.0f) {
        const Vector2 measured = measure();
        if (size.x <= 0.0f) size.x = measured.x;
        if (size.y <= 0.0f) size.y = measured.y;
    }

    const auto anchor_id = static_cast<unsigned char>(anchor);
    const int column = anchor_id % 3;
    const int row = anchor_id / 3;

    float x;
    switch (column) {
        case 1:  x = parent_rect.x + (parent_rect.width - size.x) * 0.5f + bounds.x; break;
        case 2:  x = parent_rect.x + parent_rect.width - size.x - bounds.x;          break;
        default: x = parent_rect.x + bounds.x;
    }

    float y;
    switch (row) {
        case 1:  y = parent_rect.y + (parent_rect.height - size.y) * 0.5f + bounds.y; break;
        case 2:  y = parent_rect.y + parent_rect.height - size.y - bounds.y;          break;
        default: y = parent_rect.y + bounds.y;
    }

    screen_rect = Rectangle{x, y, size.x, size.y};

    // Children are placed inside the rectangle that was just resolved.
    // The child chain is walked here, so a node never resolves its own siblings.
    for (ViewNode* node = child.get(); node != nullptr; node = node->sibling.get()) {
        if (auto* ui_child = dynamic_cast<UINode*>(node))
            ui_child->resolve_layout(screen_rect);
    }
}

void UINode::render() {
    if (!isEnabled) return;

    draw();

    // Painter's order: an element is covered by its own children, and a child
    // added later is drawn over one added earlier. This is why the child branch
    // is walked before the sibling one, unlike in ViewNode::render().
    if (child) {
        UIView* v = get_view();
        const bool clipping = clip_children && v != nullptr;
        if (clipping) v->push_clip(screen_rect);
        child->render();
        if (clipping) v->pop_clip();
    }
    if (sibling) sibling->render();
}

bool UINode::contains(const Vector2 point) const {
    return CheckCollisionPointRec(point, screen_rect);
}

UINode* UINode::hit_test(const Vector2 point) {
    if (!isEnabled) return nullptr;

    UINode* hit = (blocks_mouse && contains(point)) ? this : nullptr;

    // Children that are clipped away cannot be seen, so they cannot be hit
    if (clip_children && !contains(point)) return hit;

    for (ViewNode* node = child.get(); node != nullptr; node = node->sibling.get()) {
        if (auto* ui_child = dynamic_cast<UINode*>(node)) {
            // Walking in draw order and keeping the last hit leaves us with the
            // topmost element under the point.
            if (UINode* child_hit = ui_child->hit_test(point)) hit = child_hit;
        }
    }
    return hit;
}
