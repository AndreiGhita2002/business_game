//
// Created by Andrei Ghita on 03.10.2025.
//

#include "VoxelEditor.hpp"

#include "main.hpp"

void VoxelEditor::update() {
    if (current_grid) {
        // move grid with camera
        auto camera_delta = Vector3Subtract(global::camera.position, last_camera_pos);

        Transform new_transform = current_grid->transform;
        // new_transform.translation += camera_delta
        new_transform.translation = Vector3Add(new_transform.translation, camera_delta);

        current_grid->set_transform(new_transform);

        last_camera_pos = global::camera.position;
    }

    if (enabled && current_grid) {
        // exit editor if '1' is pressed
        if (IsKeyReleased(KEY_ONE)) {
            enabled = false;
            pop_grid();
            return;
        }

        return; // todo

        // mouse input
        bool mouse_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        Vector2 delta = GetMouseDelta();

        if (delta.x > 0.1 || delta.y > 0.1) {
            // mouse is dragged; rotate grid

        } else {
            // mouse is pressed; place or remove voxel

        }

        // calculate the axis: vector from grid to camera
        //
    } else if (enabled && !current_grid) {
        // uh idk

    } else if (!enabled) {
        // enable editor if 1 is pressed
        if (IsKeyReleased(KEY_ONE)) {
            // get grid at mouse
            Ray ray = GetScreenToWorldRay(GetMousePosition(), global::camera);

            TraceLog(LOG_DEBUG, "Ray cast: distance=%f,%f,%f to direction=%f,%f,%f",
                ray.position.x, ray.position.y, ray.position.z,
                ray.direction.x, ray.direction.y, ray.direction.z
            );

            RayCollision collision{};
            VoxelGrid* found_grid = nullptr;
            for (auto grid : global::voxel_grids) {
                for (auto model : grid->get_models()) {
                    // for (Mesh* mesh : model->model.meshes)
                    // todo make this work on multiple meshes
                    collision = GetRayCollisionMesh(ray, model->model.meshes[0], model->model.transform);

                    if (collision.hit) break;
                }
                if (collision.hit && grid->get_grid_type() == "SingleChunkGrid") {
                    found_grid = grid;
                    break;
                };
            }

            if (found_grid) {
                // grid has been found
                set_grid(found_grid);
                enabled = true;
            }
        }
    }
}

void VoxelEditor::set_grid(VoxelGrid* grid) {
    TraceLog(LOG_DEBUG, "[EDITOR] Grid set!");

    current_grid = grid;
    old_transform = grid->transform;

    // set the new grid position to be in front of the camera

    Vector3 camera_ray = Vector3Subtract(global::camera.target, global::camera.position);
    camera_ray = Vector3Normalize(camera_ray);
    camera_ray = Vector3Scale(camera_ray, zoom);

    Vector3 new_pos = Vector3Add(global::camera.position, camera_ray);

    current_grid->set_transform({
        new_pos,
        Quaternion(0.0, 0.0, 0.0, 1.0),
        Vector3(1.0, 1.0, 1.0),
    });

    last_camera_pos = global::camera.position;
}

VoxelGrid *VoxelEditor::pop_grid() {
    TraceLog(LOG_DEBUG, "[EDITOR] Grid released at: %f,%f,%f!",
        old_transform.translation.x, old_transform.translation.y, old_transform.translation.z
    );

    auto grid_p = current_grid;
    current_grid = nullptr;
    grid_p->transform = old_transform;
    return grid_p;
}

VoxelEditor::VoxelEditor() :
    enabled(false),
    current_grid(nullptr),
    old_transform({0}),
    zoom(1.0f),
    last_camera_pos()
{}
