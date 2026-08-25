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

#define VOXEL_VIEW_STR "VoxelView"

class VoxelView : public ViewNode {
public:
    std::vector<VoxelGrid*> voxel_grids;
    VoxelMap* game_map;

    raylib::Camera camera;
    raylib::Shader* voxel_shader;

    unsigned int next_light_id = 0;
    std::vector<Light> lights;
    size_t sun_light_id;
    size_t camera_light_id;
    bool move_camera_light = true;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;

    VoxelView(ViewNode* parent, raylib::Shader* shader);

    // Helper Functions
    bool isInRenderDistance(Vector3 v) const;

private:
    // Update Functions, called every tick
    void updateCamera();
    void updateLights();
    void updateVoxelMesh() const;

    // Drawing Functions
    // Should always be within a BeginMode3D()/EndMode3D() block.
    void drawVoxelScene();
    void drawVoxelModel(const VoxelGrid* grid, const ModelInfo& model_info);
};


#endif //BUSINESS_GAME_VOXELVIEW_HPP
