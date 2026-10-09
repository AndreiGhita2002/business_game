//
// Created by Andrei Ghita on 06.10.2025.
//

#include "VoxelView.hpp"

#include <algorithm>
#include <cstdlib>
#include <rlgl.h>

#include "game/Frustum.hpp"
#include "game/main.hpp"
#include "voxel/SingleChunkGrid.hpp"

// How far out the marker for a directional light is drawn, in world units. It
// has no position of its own, so this is only a place to put the sphere.
#define LIGHT_MARKER_DISTANCE 50.0f

// How far a grid's shadow is taken to reach, in world units, when working out
// which grids could shadow a model. Only used to keep volumes out of a draw
// call's list, so it errs long: a grid further than this from what it is
// shadowing stops casting onto it.
#define SHADOW_CASTER_REACH 64.0f


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
    // Then back by the window's origin, so voxel (0, 0, 0) of the volume is
    // where the window starts. Model space is the mesher's (X grid x, Y grid
    // z, Z grid y), which the shader swaps back to grid order after this.
    // raylib's MatrixMultiply applies the left one first.
    const Matrix map_matrix = transform_to_matrix(game_map->get_world_transform());
    const Matrix window = MatrixTranslate(static_cast<float>(-world_volume_origin.x),
                                          static_cast<float>(-world_volume_origin.z),
                                          static_cast<float>(-world_volume_origin.y));
    SetShaderValueMatrix(*voxel_shader, world_to_volume_loc, MatrixMultiply(MatrixInvert(map_matrix), window));

    // Cut off at the top of the ground: nothing above it can block a ray, so a
    // ray leaving this shorter box is exactly as lit as one leaving the whole
    // volume, without the walk through the sky in between. Never below one
    // layer, so the box is never empty.
    const Int3 size = world_volume.get_size();
    const int s_size[3] = {size.x, size.y, std::clamp(world_volume_top, 1, size.z)};
    SetShaderValue(*voxel_shader, volume_size_loc, s_size, SHADER_UNIFORM_IVEC3);

    // The atlas stays on its own unit for the whole frame. Which bricks in it
    // a fragment actually traces is decided per draw, in sendGridVolumes().
    grid_atlas.bind(GRID_ATLAS_TEXTURE_UNIT);
    const int atlas_unit = GRID_ATLAS_TEXTURE_UNIT;
    SetShaderValue(*voxel_shader, grid_atlas_loc, &atlas_unit, SHADER_UNIFORM_INT);
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
    const float camera_trans_speed = 24.0f * camera_speed_multiplier * GetFrameTime();
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
    // The map's voxels, which every shadow ray is traced against
    if (game_map->update_volume(world_volume, world_volume_origin)) world_volume_top = game_map->solid_top();

    // And a brick for every other grid. A grid keeps its slot for as long as it
    // lives and hands it back in its destructor, so this only ever hands out
    // slots to grids that are new here.
    if (!grid_atlas.is_created()) return;

    for (VoxelGrid* grid : voxel_grids) {
        if (grid == nullptr || grid == game_map) continue;

        // Too big for one brick. A VoxelMap says so, and is already in the
        // world volume above.
        const VoxelChunk* chunk = grid->get_volume_chunk();
        if (chunk == nullptr) continue;

        if (grid->volume_slot < 0) {
            grid->volume_slot = grid_atlas.acquire_slot();
            // The atlas is full, which it has already complained about. The
            // grid simply casts no shadow.
            if (grid->volume_slot < 0) continue;
            grid->volume_dirty = true;
        }

        if (grid->volume_dirty) {
            grid_atlas.upload(grid->volume_slot, *chunk);
            grid->volume_dirty = false;
        }
    }
}

void VoxelView::release_grid_volume(const int slot) {
    grid_atlas.release_slot(slot);
}

void VoxelView::set_volume_window(const Int3 origin, const Int3 size) {
    // Whole chunks, so that a chunk upload never hangs over the texture's edge
    const auto down = [](const int v) { return std::max(0, v) / CHUNK_SIZE * CHUNK_SIZE; };
    const auto up = [](const int v) { return (std::max(0, v) + CHUNK_SIZE - 1) / CHUNK_SIZE * CHUNK_SIZE; };

    const Int3 chunk_count = game_map->get_chunk_count();
    const Int3 map_voxels{chunk_count.x * CHUNK_SIZE, chunk_count.y * CHUNK_SIZE, chunk_count.z * CHUNK_SIZE};

    const Int3 start{std::min(down(origin.x), map_voxels.x), std::min(down(origin.y), map_voxels.y), 0};
    const Int3 end{
        std::min({up(origin.x + size.x), map_voxels.x, start.x + MAX_WORLD_VOLUME_SIDE}),
        std::min({up(origin.y + size.y), map_voxels.y, start.y + MAX_WORLD_VOLUME_SIDE}),
        std::min(up(size.z), map_voxels.z),
    };

    world_volume_origin = start;
    // Never empty, so the shader always has a texture to read
    const Int3 volume_size{std::max(end.x - start.x, CHUNK_SIZE), std::max(end.y - start.y, CHUNK_SIZE),
                           std::max(end.z, CHUNK_SIZE)};

    // The map's chunks copied into the window here and sent in the one upload
    // that makes the texture, rather than as a GL call each on the next frame.
    // calloc, as a fresh volume is air wherever no chunk is.
    auto* voxels = static_cast<VoxelID*>(std::calloc(
        static_cast<size_t>(volume_size.x) * static_cast<size_t>(volume_size.y) * static_cast<size_t>(volume_size.z), 1));
    if (voxels != nullptr) game_map->copy_window(voxels, start, volume_size);
    else for (auto& [chunk_pos, dirty] : game_map->chunk_volume_dirty) dirty = true;
    world_volume.create(volume_size, voxels);
    std::free(voxels);
    world_volume_top = game_map->solid_top();
}

VoxelView::VoxelView(ViewNode* parent, raylib::Shader* shader, const Int3 map_size)
    : ViewNode(parent), voxel_shader(shader)
{
    // Camera. Just off the map's corner and above the hills there, which a map
    // with ground up to 128 voxels tall would otherwise put it inside of.
    camera = {
        {
            { -8.0f, 40.0f, -8.0f },
            { 32.0f, 0.0f, 32.0f },
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

    // Where the volume uniforms sit in the shader
    volume_loc = GetShaderLocation(*voxel_shader, "worldVolume");
    world_to_volume_loc = GetShaderLocation(*voxel_shader, "worldToVolume");
    volume_size_loc = GetShaderLocation(*voxel_shader, "volumeSize");
    grid_atlas_loc = GetShaderLocation(*voxel_shader, "gridAtlas");
    grid_volume_count_loc = GetShaderLocation(*voxel_shader, "gridVolumeCount");
    for (int i = 0; i < MAX_GRID_VOLUMES; ++i) {
        grid_volume_matrix_loc[i] = GetShaderLocation(*voxel_shader,
            TextFormat("gridVolumes[%i].worldToGrid", i));
        grid_volume_origin_loc[i] = GetShaderLocation(*voxel_shader,
            TextFormat("gridVolumes[%i].atlasOrigin", i));
    }

    // Voxels
    voxel_grids = std::vector<VoxelGrid*>();

    game_map = new VoxelMap(this, static_cast<uint32_t>(map_size.x), static_cast<uint32_t>(map_size.y),
                            static_cast<uint32_t>(map_size.z));
    voxel_grids.emplace_back(game_map);

    // The voxels the shadow rays are traced against: the whole map when it
    // fits in one window. A world map does not, and starts with a window of a
    // single chunk rather than a huge one the game is about to throw away: it
    // puts the window over its land with set_volume_window(). The map fills
    // it on the first update.
    const bool fits = map_size.x <= MAX_WORLD_VOLUME_SIDE && map_size.y <= MAX_WORLD_VOLUME_SIDE;
    set_volume_window(Int3{0, 0, 0}, fits ? map_size : Int3{CHUNK_SIZE, CHUNK_SIZE, CHUNK_SIZE});

    // A brick each for every other grid, so that a vehicle casts a shadow and
    // shadows itself. Slots are handed out as the grids are first uploaded.
    grid_atlas.create();

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

VoxelView::~VoxelView() {
    // Children before parents, the reverse of the order they were added in.
    // Either order would be safe, as a grid's destructor unhooks it from both
    // ends of the tree, but this way nothing is re-parented on its way out.
    // Each destructor also hands its atlas slot back, and grid_atlas is a
    // member, so it is still alive while this body runs.
    for (auto it = voxel_grids.rbegin(); it != voxel_grids.rend(); ++it) delete *it;
    voxel_grids.clear();
    game_map = nullptr;
}

void VoxelView::add_grids(const std::vector<VoxelGrid*>& grids) {
    voxel_grids.insert(voxel_grids.end(), grids.begin(), grids.end());
}

void VoxelView::remove_grids(const std::vector<VoxelGrid*>& grids) {
    // Before the grids are forgotten, while every pointer is still good
    for (const GridRemovalListener& listener : removal_listeners) listener(grids);

    std::erase_if(voxel_grids, [&grids](const VoxelGrid* g) {
        return std::find(grids.begin(), grids.end(), g) != grids.end();
    });
}

void VoxelView::add_grid_removal_listener(GridRemovalListener listener) {
    removal_listeners.push_back(std::move(listener));
}

void VoxelView::drawVoxelScene() {
    // Taken from rlgl inside the 3D block, as the water does, so the culling
    // agrees with the projection BeginMode3D() set up. Shadows are traced
    // through the volumes, not drawn, so a chunk off screen casts its shadow
    // all the same.
    const Frustum frustum = frustum_from_matrix(MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()));

    draw_list.clear();
    for (VoxelGrid* grid : voxel_grids) {
        for (ModelInfo* model_info : grid->get_models()) {
            // SingleChunkGrid reports a null model until it has been meshed,
            // which is the case for one frame when a grid is added after this
            // view's update has run (the editor's "New Grid" does this). A
            // model out of render distance is still listed, switched off.
            if (model_info == nullptr || !model_info->do_render) continue;

            // Built by voxel_model_matrix(), which the editor's ray casts also
            // use, so a click always lands on what is actually drawn. Every
            // model is meshed inside its chunk's cube, so that cube is its box.
            const Matrix matrix = voxel_model_matrix(grid, *model_info);
            const BoundingBox box = voxel_box_bounds(matrix, CHUNK_SIZE);
            if (!frustum_contains_box(frustum, box)) continue;

            const Vector3 centre = Vector3Scale(Vector3Add(box.min, box.max), 0.5f);
            draw_list.push_back(DrawItem{model_info, matrix, box,
                                         Vector3DistanceSqr(camera.position, centre)});
        }
    }

    // Nearest first. Every fragment marches a shadow ray, and one drawn over
    // later has paid for its march for nothing; near ones first lets the depth
    // test throw the hidden ones away before the fragment shader runs.
    std::sort(draw_list.begin(), draw_list.end(), [](const DrawItem& a, const DrawItem& b) {
        return a.distance_sq < b.distance_sq;
    });

    gatherShadowCasters();
    for (const DrawItem& item : draw_list) drawVoxelModel(item);
}

void VoxelView::drawVoxelModel(const DrawItem& item) {
    // The Model is copied by value, as DrawModel would do anyway, and only the
    // matrix on the copy is replaced. The meshes are shared, not duplicated.
    Model model = item.model->model;
    model.transform = item.matrix;

    // Which grids could throw a shadow onto this model. Sent per draw, as the
    // answer is different for every chunk.
    sendGridVolumes(item.box);

    // Drawing the model
    DrawModel(model, Vector3{0.0f, 0.0f, 0.0f}, 1.0f, WHITE);

    // Drawing wires
    // DrawModelWires(model, Vector3{0.0f, 0.0f, 0.0f}, 1.0f, DARKGRAY);
}

void VoxelView::gatherShadowCasters() {
    shadow_casters.clear();
    if (!grid_atlas.is_created() || !lights[sun_light_id].enabled) return;

    for (const VoxelGrid* grid : voxel_grids) {
        // No brick, nothing to trace. The map is always in this state, as it
        // lives in the world volume instead.
        if (grid == nullptr || grid->volume_slot < 0) continue;

        const Matrix grid_matrix = transform_to_matrix(grid->get_world_transform());
        const Int3 origin = grid_atlas.slot_origin(grid->volume_slot);
        shadow_casters.push_back(ShadowCaster{
            voxel_box_bounds(grid_matrix, CHUNK_SIZE),
            // The matrix that undoes the grid, so the shader can trace the
            // ray in the grid's own space where its voxels are axis aligned
            // again
            MatrixInvert(grid_matrix),
            {static_cast<float>(origin.x), static_cast<float>(origin.y), static_cast<float>(origin.z)},
        });
    }
}

void VoxelView::sendGridVolumes(const BoundingBox& receiver_box) {
    int count = 0;

    if (!shadow_casters.empty()) {
        // A grid is worth tracing only if its own box, dragged along the
        // light, still reaches the model: everything else is left out of the
        // list here rather than walked away from per pixel. A grid always
        // reaches itself, which is what shadows a vehicle with its own shape.
        const Vector3 sun_direction = lights[sun_light_id].get_direction();

        for (const ShadowCaster& caster : shadow_casters) {
            if (count >= MAX_GRID_VOLUMES) break;
            if (!box_casts_onto(caster.box, receiver_box, sun_direction, SHADOW_CASTER_REACH)) continue;

            SetShaderValueMatrix(*voxel_shader, grid_volume_matrix_loc[count], caster.world_to_grid);
            SetShaderValue(*voxel_shader, grid_volume_origin_loc[count], caster.atlas_origin, SHADER_UNIFORM_VEC3);
            count++;
        }
    }

    SetShaderValue(*voxel_shader, grid_volume_count_loc, &count, SHADER_UNIFORM_INT);
}

bool VoxelView::isInRenderDistance(const Vector3 v) const {
    // TODO (optimisation) this should be rewritten so that it doesn't use a sqrt operation
    return Vector3Distance(camera.position, v) <= global::render_distance
    || !global::limit_render_distance;
}
