//
// Created by Andrei Ghita on 03.10.2025.
//

#ifndef BUSINESS_GAME_VOXELEDITOR_HPP
#define BUSINESS_GAME_VOXELEDITOR_HPP

#include "game/ViewNode.hpp"
#include "voxel/VoxelGrid.hpp"

#define VOXEL_EDITOR_STR "VoxelEditor"

class VoxelEditor : public ViewNode {
public:
    std::string &get_view_type() override;

    void update(float delta_time) override;
    void set_grid(VoxelGrid* grid);
    VoxelGrid* pop_grid();

    explicit VoxelEditor(VoxelView* parent_view);

private:
    VoxelView* parent_view; // same as parent
    VoxelGrid* selected_grid;
    Transform old_transform;
    float zoom;

    Vector3 last_camera_pos;

    /**
     * Moves the selected grid to be `dist` units in front of the camera.
     * This function should be called every time the camera moves.
     * Note: this function does not change the rotation of the grid.
     *
     * @param dist - how many units should be between camera pos and selected grid
     */
    void keep_grid_in_place(float dist) const;
};


#endif //BUSINESS_GAME_VOXELEDITOR_HPP