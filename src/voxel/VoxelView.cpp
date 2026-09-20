//
// Created by Andrei Ghita on 06.10.2025.
//

#include "VoxelView.hpp"

#include <rlgl.h>

#include "game/main.hpp"
#include "voxel/SingleChunkGrid.hpp"

// How far out the marker for a directional light is drawn, in world units. It
// has no position of its own, so this is only a place to put the sphere.
#define LIGHT_MARKER_DISTANCE 50.0f


std::string & VoxelView::get_view_type() {
    static std::string TYPE = VOXEL_VIEW_STR;
    return TYPE;
}

void VoxelView::update(float delta_time) {
    updateCamera();
    updateVoxelMesh();
    updateVolumes();
    updateLights();

    ViewNode::update(delta_time);
}

void VoxelView::render() {
    // There is no shadow pass any more. The shader traces a shadow ray through
    // the world's voxels for every fragment it lights, so the scene is drawn
    // once and the only thing to set up first is the volume those rays walk.
    // See resources/shaders/lighting.fs.
    //
    // Note: the frame's BeginDrawing()/EndDrawing() block is opened by
    // global::mainLoop(), so that the UI views can draw on top of this one.
    rlEnableShader(voxel_shader->id);
    bindWorldVolume();

    BeginMode3D(camera); {
        drawVoxelScene();
        drawLightMarkers();
    }
    EndMode3D();

    // The UI, which needs to be drawn on top of the voxel scene, so at the end
    ViewNode::render();
}

void VoxelView::bindWorldVolume() const {
    if (!world_volume.is_created()) return;

    world_volume.bind(WORLD_VOLUME_TEXTURE_UNIT);
    const int unit = WORLD_VOLUME_TEXTURE_UNIT;
    SetShaderValue(*voxel_shader, volume_loc, &unit, SHADER_UNIFORM_INT);

    // World space back into the map's own space, which is where its voxels
    // are. Worked out every frame rather than cached, so that moving the map
    // moves its shadows with it, the same way get_world_transform() is.
    const Matrix map_matrix = transform_to_matrix(game_map->get_world_transform());
    SetShaderValueMatrix(*voxel_shader, world_to_volume_loc, MatrixInvert(map_matrix));

    const Int3 size = world_volume.get_size();
    const int s_size[3] = {size.x, size.y, size.z};
    SetShaderValue(*voxel_shader, volume_size_loc, s_size, SHADER_UNIFORM_IVEC3);
}

void VoxelView::drawLightMarkers() const {
    // A directional light has no position, so its marker is put out along its
    // own direction from whatever the camera is looking at.
    for (const Light& light : lights) {
        const Vector3 direction = light.get_direction();
        const Vector3 marker = {
            camera.target.x - direction.x * LIGHT_MARKER_DISTANCE,
            camera.target.y - direction.y * LIGHT_MARKER_DISTANCE,
            camera.target.z - direction.z * LIGHT_MARKER_DISTANCE,
        };

        if (light.enabled) DrawSphereEx(marker, 1.0f, 8, 8, light.color);
        else DrawSphereWires(marker, 1.0f, 8, 8, ColorAlpha(light.color, 0.3f));
    }
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
    // The sun's toggle is also a button in the bottom left, and its angle in
    // the sky is two rows in the shader menu. Both routes write the same light,
    // so they stay in step.
    if (IsKeyReleased(KEY_U)) lights[sun_light_id].enabled = !lights[sun_light_id].enabled;

    // Update
    for (const Light &light : lights) {
        light.update(*voxel_shader);
    }
}

void VoxelView::updateVoxelMesh() const {
    for (VoxelGrid* grid : voxel_grids) {
        grid->update_models();
    }
}

void VoxelView::updateVolumes() {
    // Only the map is traced for now, so only the map has a volume. A grid with
    // a transform of its own needs one of its own, which is a later step.
    game_map->update_volume(world_volume);
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
    // One light, the sun, which is an angle in the sky and a colour. Shadows
    // are traced through the world volume in the fragment shader, so a light
    // carries no shadow map and nothing has to be fitted around the scene.
    // The vector is reserved at its final size and never grown again, because
    // the shader menu edits the sun's angles through a pointer into it.
    lights = std::vector<Light>();
    lights.reserve(2);
    sun_light_id = Light::create_directional(
        55.0f, 135.0f, WHITE,
        *voxel_shader, &lights, next_light_id++);

    // Where the world volume's uniforms sit in the shader
    volume_loc = GetShaderLocation(*voxel_shader, "worldVolume");
    world_to_volume_loc = GetShaderLocation(*voxel_shader, "worldToVolume");
    volume_size_loc = GetShaderLocation(*voxel_shader, "volumeSize");

    // Voxels
    voxel_grids = std::vector<VoxelGrid*>();

    game_map = new VoxelMap(this, 128, 128);
    voxel_grids.emplace_back(game_map);

    // The voxels the shadow rays are traced against. Sized to whole chunks
    // rather than to the map, so that a chunk upload can never hang over the
    // edge of the texture. The map fills it on the first update.
    const Int2 chunk_count = game_map->get_chunk_count();
    world_volume.create(Int3{chunk_count.x * CHUNK_SIZE, chunk_count.y * CHUNK_SIZE, CHUNK_SIZE});

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
