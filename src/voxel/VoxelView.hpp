//
// Created by Andrei Ghita on 06.10.2025.
//

#ifndef BUSINESS_GAME_VOXELVIEW_HPP
#define BUSINESS_GAME_VOXELVIEW_HPP
#include <Camera3D.hpp>
#include <Shader.hpp>

#include "game/Light.hpp"
#include "game/ViewNode.hpp"
#include "voxel/VoxelGrid.hpp"
#include "voxel/VoxelMap.hpp"
#include "voxel/VoxelVolume.hpp"

#define VOXEL_VIEW_STR "VoxelView"

// The texture unit the world volume is bound to. raylib hands out units from 0
// upwards to a material's own maps as it draws a model, so this sits above
// anything it will use.
#define WORLD_VOLUME_TEXTURE_UNIT 12

class VoxelView : public ViewNode {
public:
    std::vector<VoxelGrid*> voxel_grids;
    VoxelMap* game_map;

    // The map's voxels on the GPU, which is what the lighting shader walks when
    // it traces a shadow ray. Only the map is in it: a grid with a transform of
    // its own needs a volume of its own, which is a later step, so a vehicle
    // neither casts a shadow nor shadows itself yet.
    VoxelVolume world_volume;

    raylib::Camera camera;
    raylib::Shader* voxel_shader;

    unsigned int next_light_id = 0;
    std::vector<Light> lights;
    size_t sun_light_id;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;

    VoxelView(ViewNode* parent, raylib::Shader* shader);

    // Helper Functions
    bool isInRenderDistance(Vector3 v) const;

private:
    // Where the world volume's uniforms sit in the shader, looked up once
    int volume_loc{-1};
    int world_to_volume_loc{-1};
    int volume_size_loc{-1};

    // Update Functions, called every tick
    void updateCamera();
    void updateLights();
    void updateVoxelMesh() const;
    void updateVolumes();

    // Drawing Functions
    // Should always be within a BeginMode3D()/EndMode3D() block.
    void drawVoxelScene();
    void drawVoxelModel(const VoxelGrid* grid, const ModelInfo& model_info);
    void drawLightMarkers() const;

    // Puts the world volume and its uniforms on the shader for this frame
    void bindWorldVolume() const;
};


#endif //BUSINESS_GAME_VOXELVIEW_HPP
