//
// Created by Andrei Ghita on 03.10.2025.
//

#ifndef BUSINESS_GAME_VOXELEDITOR_HPP
#define BUSINESS_GAME_VOXELEDITOR_HPP
#include "voxel/VoxelGrid.hpp"


class VoxelEditor {
public:
    bool enabled;

    void update();
    void set_grid(VoxelGrid* grid);
    VoxelGrid* pop_grid();

    VoxelEditor();
    ~VoxelEditor() = default;

private:
    VoxelGrid* current_grid;
    Transform old_transform;
    float zoom;

    Vector3 last_camera_pos;
};


#endif //BUSINESS_GAME_VOXELEDITOR_HPP