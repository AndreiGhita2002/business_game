//
// Created by Andrei Ghita on 06.10.2025.
//

#ifndef BUSINESS_GAME_VOXELVIEW_HPP
#define BUSINESS_GAME_VOXELVIEW_HPP
#include <Camera3D.hpp>
#include <Shader.hpp>
#include <functional>

#include "entity/GridSink.hpp"
#include "game/Light.hpp"
#include "game/ViewNode.hpp"
#include "voxel/VoxelBrickAtlas.hpp"
#include "voxel/VoxelGrid.hpp"
#include "voxel/VoxelMap.hpp"
#include "voxel/VoxelVolume.hpp"

#define VOXEL_VIEW_STR "VoxelView"

// The texture units the two shadow volumes are bound to. raylib hands out units
// from 0 upwards to a material's own maps as it draws a model, so these sit
// above anything it will use.
#define WORLD_VOLUME_TEXTURE_UNIT 12
#define GRID_ATLAS_TEXTURE_UNIT 13
// The world volume's coarse occupancy, which shadow rays skip empty space with
#define WORLD_COARSE_TEXTURE_UNIT 14

// How many grid volumes one draw call can be traced against. Patched into the
// shader as MAX_GRID_VOLUMES by global::loadAndPatchShader(), so the two cannot
// drift apart. A model with more casters reaching it than this keeps the ones
// that come first in voxel_grids and loses the rest, which is only a problem
// once there are more vehicles crowded around one chunk than this allows.
#define MAX_GRID_VOLUMES 8

// The largest the world volume's window is made along x and y, in voxels.
// 1024 by 1024 by a map 256 tall is 256 MiB at a byte a voxel; an island of
// four cells in a line, the longest there is, is exactly 1024 long.
#define MAX_WORLD_VOLUME_SIDE 1024

/**
 * Called with the grids that are about to stop being drawn and be deleted, so
 * anything holding a pointer to one (a menu's selection, say) can let go.
 */
using GridRemovalListener = std::function<void(const std::vector<VoxelGrid*>& removed)>;

class VoxelView : public ViewNode, public GridSink {
public:
    // Every grid that is updated and drawn. The map and the test grid made in
    // the constructor, and any grid the editor's "New Grid" button makes, are
    // this view's own and are deleted with it; an entity's
    // grids belong to the entity, which hands them in and takes them back out
    // through the GridSink calls below.
    std::vector<VoxelGrid*> voxel_grids;
    VoxelMap* game_map;

    // The map's voxels on the GPU, which is what the lighting shader walks when
    // it traces a shadow ray. A window of the map rather than all of it, from
    // world_volume_origin (voxels, whole chunks, z always 0): a whole world is
    // far bigger than a 3D texture can be. Outside it nothing casts a shadow.
    // set_volume_window() moves it.
    VoxelVolume world_volume;
    Int3 world_volume_origin{0, 0, 0};
    // The same window at one voxel per WORLD_COARSE^3 (VoxelVolume.hpp), 1
    // where any of them is solid: march_world() in lighting.fs strides across
    // the empty ones instead of walking every voxel
    VoxelVolume world_coarse;
    // How much of it, from the bottom up, holds anything: the map's
    // solid_top(), refreshed whenever a chunk is uploaded. The shader is told
    // the volume ends there, so a shadow ray stops at the top of the ground
    // instead of walking all the empty sky above it to the ceiling.
    int world_volume_top = 0;

    // Every other grid's voxels, a brick each. These are traced after the world
    // volume, so a vehicle casts a shadow onto the terrain, onto other
    // vehicles, and onto itself.
    VoxelBrickAtlas grid_atlas;

    raylib::Camera camera;
    // Scales how fast the movement keys (WASD, F/C) carry the camera, from 24
    // units a second at 1. Turning (Q/E) keeps its own speed. Edited in the
    // game settings menu.
    float camera_speed_multiplier = 5.0f;
    raylib::Shader* voxel_shader;

    unsigned int next_light_id = 0;
    std::vector<Light> lights;
    size_t sun_light_id;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;

    /**
     * @param map_size: the map's size in voxels, z up. It starts as air; the
     *        game draws the simulation's terrain into it, see
     *        entity/TerrainVoxels.hpp.
     */
    VoxelView(ViewNode* parent, raylib::Shader* shader, Int3 map_size);

    /**
     * Deletes the grids still in voxel_grids, which by then should only be the
     * view's own: everything that lent it grids (the entities) has to be gone
     * first, see global::shutdown().
     */
    ~VoxelView() override;

    // --- GridSink ---
    void add_grids(const std::vector<VoxelGrid*>& grids) override;
    /** Tells every removal listener first, then forgets the grids. Does not delete them. */
    void remove_grids(const std::vector<VoxelGrid*>& grids) override;

    void add_grid_removal_listener(GridRemovalListener listener);

    /**
     * Puts the shadow volume over the map's voxels from `origin` to `origin +
     * size`, in voxels, both brought out to whole chunks and kept inside the
     * map, with z running from the bottom of the map. The volume is made
     * again, empty, and every chunk of the map is marked to go back into it.
     * Needs the GL context.
     */
    void set_volume_window(Int3 origin, Int3 size);

    // Helper Functions
    bool isInRenderDistance(Vector3 v) const;

    /**
     * Hands an atlas slot back, called by ~VoxelGrid as a grid goes away. The
     * grid holds the slot, so this is the one way back into the atlas.
     */
    void release_grid_volume(int slot);

private:
    std::vector<GridRemovalListener> removal_listeners;

    // Where the volume uniforms sit in the shader, looked up once
    int volume_loc{-1};
    int world_to_volume_loc{-1};
    int volume_size_loc{-1};
    int world_coarse_loc{-1};
    int grid_atlas_loc{-1};
    int grid_volume_count_loc{-1};
    int grid_volume_matrix_loc[MAX_GRID_VOLUMES]{};
    int grid_volume_origin_loc[MAX_GRID_VOLUMES]{};

    // Update Functions, called every tick
    void updateCamera();
    void updateLights();
    void updateVoxelMesh() const;
    void updateVolumes();

    // A model that passed the camera test this frame, with what drawing it needs
    struct DrawItem {
        const ModelInfo* model;
        Matrix matrix;
        BoundingBox box;
        // Squared distance from the camera to the box's centre, for the order
        float distance_sq;
    };
    // A grid with an atlas brick, worked out once a frame rather than once
    // for every model it might shadow
    struct ShadowCaster {
        BoundingBox box;
        Matrix world_to_grid;
        float atlas_origin[3];
    };
    // Kept between frames so their memory is too
    std::vector<DrawItem> draw_list;
    std::vector<ShadowCaster> shadow_casters;

    // Drawing Functions
    // Should always be within a BeginMode3D()/EndMode3D() block.
    void drawVoxelScene();
    void drawVoxelModel(const DrawItem& item);
    void drawLightMarkers() const;

    // Puts the volumes and their uniforms on the shader for this frame
    void bindWorldVolume() const;

    // Fills shadow_casters for this frame
    void gatherShadowCasters();

    // Picks the casters whose shadows could land on a model with this box and
    // sends them, so that a fragment only traces the few volumes that could
    // reach it
    void sendGridVolumes(const BoundingBox& receiver_box);
};


#endif //BUSINESS_GAME_VOXELVIEW_HPP
