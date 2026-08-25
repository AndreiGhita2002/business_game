//
// Created by Andrei Ghita on 03.10.2025.
//

#include "VoxelEditor.hpp"

#include <cfloat>

#include "game/main.hpp"
#include "voxel/SingleChunkGrid.hpp"

std::string& VoxelEditor::get_view_type() {
    static std::string TYPE = VIEW_NODE_STR;
    return TYPE;
}

void VoxelEditor::update(float delta_time) {
    if (!isEnabled) return;

    if (selected_grid) {
        // move grid with camera
        keep_grid_in_place(3);

        // exit editor if '1' is pressed
        if (IsKeyReleased(KEY_ONE)) {
            pop_grid();
            return;
        }
        //...

    } else if (IsKeyReleased(KEY_ONE)) {
        // try enabling the editor if 1 is pressed
        // get grid at mouse
        Ray ray = GetScreenToWorldRay(GetMousePosition(), parent_view->camera);
        if (VoxelGrid* grid = find_grid_on_ray(ray, &parent_view->voxel_grids, SINGLE_CHUNK_GRID_STR))
            set_grid(grid);
    }
    ViewNode::update(delta_time);
}

void VoxelEditor::set_grid(VoxelGrid* grid) {
    TraceLog(LOG_DEBUG, "[EDITOR] Grid set! Type: %s", &grid->get_grid_type());

    selected_grid = grid;
    old_transform = grid->transform;

    // set the new grid position to be in front of the camera

    // Vector3 camera_ray = Vector3Subtract(parent_view->camera.target, parent_view->camera.position);
    // camera_ray = Vector3Normalize(camera_ray);
    // camera_ray = Vector3Scale(camera_ray, zoom);
    //
    // Vector3 new_pos = Vector3Add(parent_view->camera.position, camera_ray);

    // selected_grid->set_transform({
    //     new_pos,
    //     Quaternion(0.0, 0.0, 0.0, 1.0),
    //     Vector3(1.0, 1.0, 1.0),
    // });

    last_camera_pos = parent_view->camera.position; //??what was the point of this??
}

VoxelGrid *VoxelEditor::pop_grid() {
    TraceLog(LOG_DEBUG, "[EDITOR] Grid released at: %f,%f,%f!",
        old_transform.translation.x, old_transform.translation.y, old_transform.translation.z
    );

    auto grid_p = selected_grid;
    selected_grid = nullptr;
    grid_p->transform = old_transform;
    return grid_p;
}

void VoxelEditor::keep_grid_in_place(float dist) const {
    const Camera3D& camera = parent_view->camera;

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    Vector3 new_grid_pos = Vector3Add(camera.position, Vector3Scale(forward, dist));

    selected_grid->transform.translation = new_grid_pos;
}

VoxelEditor::VoxelEditor(VoxelView* parent_view) :
    ViewNode(parent_view),
    selected_grid(nullptr),
    old_transform({0}),
    zoom(1.0f),
    last_camera_pos(),
    parent_view(parent_view)
{}
