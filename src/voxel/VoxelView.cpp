//
// Created by Andrei Ghita on 06.10.2025.
//

#include "VoxelView.hpp"

#include <rlgl.h>

#include "game/main.hpp"
#include "voxel/SingleChunkGrid.hpp"


std::string & VoxelView::get_view_type() {
    static std::string TYPE = VOXEL_VIEW_STR;
    return TYPE;
}

void VoxelView::update(float delta_time) {
    updateCamera();
    updateVoxelMesh();
    updateLights();

    ViewNode::update(delta_time);
}

void VoxelView::render() {
    Matrix light_view = {};
    Matrix light_proj = {};

    // PASS 1: Render all objects into the shadow map render texture
    // (render textures may be used inside the frame's drawing block)
    //
    // Only back faces are written to the shadow map. The recorded depth then
    // belongs to the far side of a solid, a whole voxel away from the surface
    // being lit, so a surface can no longer shadow itself. Without this, a
    // large light box needs a bias so big that shadows come away from their
    // casters. It relies on the voxel meshes being closed, which they are:
    // the mesher emits a face wherever the neighbour is air or out of chunk.
    // The batch is flushed first, as the cull mode is immediate GL state while
    // the batch is deferred.
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_FRONT);
    // A tight depth range around the light. The main camera's 0.01 to 1000
    // would put nearly all of the depth precision into empty space.
    rlSetClipPlanes(SHADOW_NEAR, SHADOW_FAR);

    for (Light& light : lights) {
        BeginTextureMode(*light.shadow_map); {
            ClearBackground(WHITE);
            if (light.enabled) {
                BeginMode3D(light.light_camera); {
                    light_view = rlGetMatrixModelview();
                    light_proj = rlGetMatrixProjection();
                    drawVoxelScene();
                }
                EndMode3D();
            }
        }
        EndTextureMode();
        // Update lightVP
        light.light_view_proj = MatrixMultiply(light_view, light_proj);
    }

    // Back to the settings the main camera pass expects
    rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
    rlDrawRenderBatchActive();
    rlSetCullFace(RL_CULL_FACE_BACK);

    // PASS 2: Drawing
    // Note: the frame's BeginDrawing()/EndDrawing() block is opened by
    // global::mainLoop(), so that the UI views can draw on top of this one.
    rlEnableShader(voxel_shader->id);
    for (Light& light : lights) {
        rlActiveTextureSlot(light.texture_loc);
        rlEnableTexture(light.shadow_map->depth.id);
        rlSetUniform(light.shadow_map_loc, &light.texture_loc, SHADER_UNIFORM_INT, 1);
        SetShaderValueMatrix(*voxel_shader, light.vp_loc, light.light_view_proj);
    }
    BeginMode3D(camera); {
        drawVoxelScene();

        // Draw spheres to show where the lights are
        for (Light& light : lights) {
            if (light.enabled) DrawSphereEx(light.position, 0.2f, 8, 8, light.color);
            // only draw disabled light if it is not the light camera while it is attached to camera
            else if (light.id != this->camera_light_id || !this->move_camera_light)
                DrawSphereWires(light.position, 0.2f, 8, 8, ColorAlpha(light.color, 0.3f));
        }
    }
    EndMode3D();

    // PASS 3: the UI
    // which needs to be drawn on top if the voxel scene, so at the end
    ViewNode::render();
}

void VoxelView::updateCamera() {
    const float camera_trans_speed = 24.0f * GetFrameTime();
    const float camera_pan_speed  = 6.0f * GetFrameTime();

    // --- Build camera-relative basis on the XZ plane ---
    float dx = camera.target.x - camera.position.x;
    float dz = camera.target.z - camera.position.z;

    // Forward (XZ only)
    float fLen = sqrtf(dx*dx + dz*dz);
    if (fLen < 1e-6f) {
        // degenerate: point some default forward to avoid NaNs
        dx = 0.0f; dz = -1.0f; fLen = 1.0f;
    }
    float fx = dx / fLen;
    float fz = dz / fLen;

    // Right (perpendicular on XZ): rotate forward 90° clockwise around Y
    float rx =  fz;
    float rz = -fx;

    // --- Input to forward/strafe amounts ---
    float fwd = 0.0f, strafe = 0.0f;
    if (IsKeyDown(KEY_W)) fwd += 1.0f;
    if (IsKeyDown(KEY_S)) fwd -= 1.0f;
    if (IsKeyDown(KEY_A)) strafe += 1.0f;
    if (IsKeyDown(KEY_D)) strafe -= 1.0f;

    // Combine and normalize so diagonals aren’t faster
    float mx = fx * fwd + rx * strafe;
    float mz = fz * fwd + rz * strafe;
    float mLen = sqrtf(mx*mx + mz*mz);
    if (mLen > 1e-6f) {
        mx = (mx / mLen) * camera_trans_speed;
        mz = (mz / mLen) * camera_trans_speed;

        camera.position.x += mx;
        camera.position.z += mz;
        camera.target.x   += mx;
        camera.target.z   += mz;
    }

    // --- Panning (yaw around position) ---
    if (IsKeyDown(KEY_Q) || IsKeyDown(KEY_E)) {
        float angle = IsKeyDown(KEY_Q) ? -camera_pan_speed : camera_pan_speed;

        float cosA = cosf(angle);
        float sinA = sinf(angle);

        float tdx = camera.target.x - camera.position.x;
        float tdz = camera.target.z - camera.position.z;

        float ndx = tdx * cosA - tdz * sinA;
        float ndz = tdx * sinA + tdz * cosA;

        camera.target.x = camera.position.x + ndx;
        camera.target.z = camera.position.z + ndz;
    }

    // --- Vertical movement ---
    if (IsKeyDown(KEY_F)) {
        camera.position.y += camera_trans_speed;
        camera.target.y   += camera_trans_speed;
    }
    if (IsKeyDown(KEY_C)) {
        camera.position.y -= camera_trans_speed;
        camera.target.y   -= camera_trans_speed;
    }

    // --- Shader Update ---
    float cameraPos[3] = { camera.position.x, camera.position.y, camera.position.z };
    SetShaderValue(*voxel_shader, voxel_shader->locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
}

void VoxelView::updateLights() {
    // Light Controls
    // Each of these also has a button in the bottom left, and the O and P keys
    // are mirrored by the light box rows in the shader menu. Both routes write
    // the same state, so they stay in step.
    if (IsKeyReleased(KEY_Y)) move_camera_light = !move_camera_light;
    if (IsKeyReleased(KEY_U)) lights[sun_light_id].enabled = !lights[sun_light_id].enabled;
    if (IsKeyReleased(KEY_I)) lights[camera_light_id].enabled = !lights[camera_light_id].enabled;

    // Camera Light
    if (move_camera_light) {
        lights[camera_light_id].position = camera.position;
        lights[camera_light_id].target = camera.target;
    }

    if (IsKeyPressed(KEY_O)) lights[camera_light_id].light_camera.fovy += 1.0f;
    if (IsKeyPressed(KEY_P)) lights[camera_light_id].light_camera.fovy -= 1.0f;

    // Update
    for (Light &light : lights) {
        light.update(*voxel_shader);
    }
}

void VoxelView::updateVoxelMesh() const {
    for (VoxelGrid* grid : voxel_grids) {
        grid->update_models();
    }
}

VoxelView::VoxelView(ViewNode* parent, raylib::Shader* shader)
    : ViewNode(parent), voxel_shader(shader)
{
    // Camera
    camera = {
        {
            { 10.0f, 5.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 1.0f, 0.0f },
            45.0f,
            0
        },
    };

    // Lights
    // Create lights
    lights = std::vector<Light>();
    lights.reserve(2);
    auto sun_pos = Vector3{32.0, 8.0, 32.0};
    auto sun_tgt = Vector3{48.0, 0.0, 48.0};
    camera_light_id = Light::create(
        DIRECTIONAL_LIGHT,
        camera.position, camera.target, WHITE,
        *voxel_shader, &lights, next_light_id++);
    sun_light_id = Light::create(
        DIRECTIONAL_LIGHT,
        sun_pos, sun_tgt, WHITE,
        *voxel_shader, &lights, next_light_id++);

    // Voxels
    voxel_grids = std::vector<VoxelGrid*>();

    game_map = new VoxelMap(this, 128, 128);
    voxel_grids.emplace_back(game_map);

    auto single_chunk_grid = new SingleChunkGrid(this, game_map->voxel_colours);
    *single_chunk_grid->get_voxel(Int3(0.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(1.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(2.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(3.0,0.0,0.0)) = 3;
    *single_chunk_grid->get_voxel(Int3(3.0,1.0,0.0)) = 3;
    single_chunk_grid->transform.translation = Vector3(-2.0f, 6.0f, -2.0f);
    single_chunk_grid->transform.scale = Vector3(1.0f, 1.0f, 1.0f);
    single_chunk_grid->was_updated = true;
    voxel_grids.emplace_back(single_chunk_grid);
}

void VoxelView::drawVoxelScene() {
    for (VoxelGrid* grid : voxel_grids) {
        for (ModelInfo* model_info : grid->get_models()) {
            drawVoxelModel(grid, *model_info);
        }
    }
}

void VoxelView::drawVoxelModel(const VoxelGrid* grid, const ModelInfo& model_info) {
    // The transform is built by voxel_model_matrix(), which the editor's ray
    // casts also use, so a click always lands on what is actually drawn.
    // The Model is copied by value, as DrawModel would do anyway, and only the
    // matrix on the copy is replaced. The meshes are shared, not duplicated.
    Model model = model_info.model;
    model.transform = voxel_model_matrix(grid, model_info);

    // Drawing the model
    DrawModel(model, Vector3{0.0f, 0.0f, 0.0f}, 1.0f, WHITE);

    // Drawing wires
    // DrawModelWires(model, Vector3{0.0f, 0.0f, 0.0f}, 1.0f, DARKGRAY);
}

bool VoxelView::isInRenderDistance(const Vector3 v) const {
    // TODO (optimisation) this should be rewritten so that it doesn't use a sqrt operation
    return Vector3Distance(camera.position, v) <= global::render_distance
    || !global::limit_render_distance;
}
