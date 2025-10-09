//
// Created by Andrei Ghita on 06.10.2025.
//

#ifndef BUSINESS_GAME_VIEWNODE_HPP
#define BUSINESS_GAME_VIEWNODE_HPP
#include <memory>
#include <string>

#define VIEW_NODE_STR "ViewNode"

class ViewNode {
public:
    bool isEnabled;

    ViewNode* parent;
    std::unique_ptr<ViewNode> sibling;
    std::unique_ptr<ViewNode> child;

    virtual std::string& get_view_type() {
        static std::string TYPE = VIEW_NODE_STR;
        return TYPE;
    }

    //todo ViewNode::update()/render() logic should always be applied
    // thus make children overload _update()/_render() which should be called in here
    virtual void update(float delta_time) {
        if (!isEnabled) return;

        if (sibling)
            sibling->update(delta_time);
        if (child)
            child->update(delta_time);
    }

    virtual void render() {
        if (!isEnabled) return;

        if (sibling)
            sibling->render();
        if (child)
            child->render();
    }

    void add_sibling(std::unique_ptr<ViewNode> new_sibling) {
        if (sibling) {
            sibling->add_sibling(std::move(new_sibling));
        } else {
            sibling = std::move(new_sibling);
        }
    }

    void add_child(std::unique_ptr<ViewNode> new_child) {
        new_child->parent = this;
        if (child) {
            child->add_sibling(std::move(new_child));
        } else {
            child = std::move(new_child);
        }
    }

    explicit ViewNode(ViewNode* parent = {})
      : isEnabled(true), parent(parent)
    {}

    virtual ~ViewNode() = default;
};


#endif //BUSINESS_GAME_VIEWNODE_HPP