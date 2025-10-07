//
// Created by Andrei Ghita on 06.10.2025.
//

#ifndef BUSINESS_GAME_VIEWNODE_HPP
#define BUSINESS_GAME_VIEWNODE_HPP
#include <memory>


class ViewNode {
public:
    ViewNode* parent;
    std::unique_ptr<ViewNode> sibling;
    std::unique_ptr<ViewNode> child;

    virtual void update(float delta_time) {
        if (sibling)
            sibling->update(delta_time);
        if (child)
            child->update(delta_time);
    }

    virtual void render() {
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
      : parent(parent)
    {}

    virtual ~ViewNode() = default;
};


#endif //BUSINESS_GAME_VIEWNODE_HPP