//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_WATERVIEW_HPP
#define BUSINESS_GAME_WATERVIEW_HPP
#include <Camera3D.hpp>
#include <functional>
#include <raylib.h>
#include <string>
#include <vector>

#include "game/ViewNode.hpp"

#define WATER_VIEW_STR "WaterView"

// How many units a side of a water chunk is, in the terrain's own space (one
// per voxel). Nothing here knows about the map's chunks. 64 keeps a world of
// sea down to a few hundred draw calls at most, each chunk a mesh of 64 by 64
// quads for the waves.
#define WATER_CHUNK_SIZE 64

// How far below the top of its voxel layer the water's surface sits, so that
// it is never in the same plane as the top of a column whose ground is at the
// water level, which would flicker as the two fought over the depth buffer.
// A quarter, so the waves have room to rise without a crest reaching that
// plane either (see WATER_WAVE_AMPLITUDE).
#define WATER_SURFACE_INSET 0.25f

// How far above and below the surface a chunk's box reaches when it is tested
// against the camera: room for the waves the vertex shader makes, so the
// culling does not cut the crests off at the edge of the screen. A wave
// amplitude past this would start to.
#define WATER_BOUNDS_MARGIN 0.5f

// The waves' defaults. Kept under WATER_SURFACE_INSET, so a crest never
// reaches the top of the voxel layer the water fills and fights the ground
// there for the depth buffer.
#define WATER_WAVE_AMPLITUDE 0.2f   // voxels, either side of the surface
#define WATER_WAVE_LENGTH 8.0f      // voxels, crest to crest
#define WATER_WAVE_PERIOD 3.0f      // seconds for a crest to move one wavelength
static_assert(WATER_WAVE_AMPLITUDE < WATER_SURFACE_INSET, "a crest would reach the next voxel layer");
static_assert(WATER_WAVE_AMPLITUDE <= WATER_BOUNDS_MARGIN, "the culling would cut crests off");

/**
 * One square of the water, on the ground plane of the terrain's own space: a
 * corner (X and Z there, which are the map's grid x and y) and how far it runs
 * along each. The height is the WaterView's, not the chunk's, so the level can
 * change without remaking the chunks.
 */
struct WaterChunk {
    float x;
    float z;
    float width;
    float depth;
};

/**
 * The chunks that cover a size_x by size_z area from the terrain's origin, row
 * by row. The last row and column are cut short when the size is not a whole
 * number of chunks, so the water never hangs over the edge of the area.
 */
std::vector<WaterChunk> water_chunk_layout(int size_x, int size_z, int chunk_size);

/**
 * Where the surface is, in the terrain's own Y, for water filling the voxel
 * layers 0 to `level`: just under the top of that layer (see
 * WATER_SURFACE_INSET). One unit is one voxel.
 */
float water_surface_height(int level);

/**
 * The box a chunk is tested against the camera with, at that surface height,
 * in the terrain's own space.
 */
BoundingBox water_chunk_bounds(const WaterChunk& chunk, float surface_y);

/**
 * How far a wave lifts the water at (x, z) in the terrain's own space, at
 * `time` seconds: a ripple running out from the terrain's (0, 0),
 *   amplitude * sin(2 pi r / length - 2 pi time / period)
 * where r is the distance from that point. The C++ twin of wave_height() in
 * resources/shaders/water.vs, which is what draws it: change the two together.
 */
float water_wave_height(float x, float z, float time, float amplitude, float length, float period);

/**
 * The water: a flat plane at one level over the whole map, drawn with its own
 * shader (resources/shaders/water.vs/.fs).
 *
 * Kept apart from the voxels on purpose. It is not a grid, it is not meshed
 * from voxels, it casts no shadow and is not in the shadow volumes, and the
 * voxel code does not know it exists.
 *
 * Attached to the terrain all the same: the water is laid out in the
 * terrain's own space (the space the map's chunks are meshed in) and drawn
 * through the same world matrix the terrain is, so moving, turning or scaling
 * the map carries the water with it. The level is the simulation's
 * (sim::World::water_level); the view only reads it, through `level_source`.
 *
 * The plane is cut into WATER_CHUNK_SIZE squares and only the squares the
 * camera can see are drawn. They are all the same flat square, so there is one
 * mesh, drawn once per visible chunk with that chunk's place in its matrix.
 *
 * It opens its own BeginMode3D() block with the VoxelView's camera, so it has
 * to be drawn after the VoxelView: the voxels' depth is still in the buffer,
 * which is what hides the water behind a hill. That also makes it the last
 * thing drawn in 3D, which is where anything see-through has to be.
 */
class WaterView : public ViewNode {
public:
    // The highest voxel layer the water fills. Asked every frame, so a changed
    // level shows on the next frame. main.cpp points it at the simulation;
    // until it is set the water stays at layer 0.
    std::function<int()> level_source;
    // The terrain's world matrix, the one its chunks are drawn through before
    // each chunk's own offset. Asked every frame. Until it is set the terrain
    // is taken to be at the world origin.
    std::function<Matrix()> terrain_matrix;
    // Handed to the shader as `waterColour`. The alpha is honoured: 179 is
    // 0.7, so the ground under the water shows through.
    Color colour{40, 110, 200, 179};

    // The waves, see water_wave_height(). Read every frame. Keep the amplitude
    // under WATER_SURFACE_INSET and WATER_BOUNDS_MARGIN, as the defaults are.
    float wave_amplitude = WATER_WAVE_AMPLITUDE;
    float wave_length = WATER_WAVE_LENGTH;
    float wave_period = WATER_WAVE_PERIOD;

    // The sea floor's colour, drawn opaque under the water. Unlit, so it is a
    // little darker than the stone it stands in for.
    Color floor_colour{104, 104, 100, 255};

    /**
     * @param camera: the camera to draw with and to cull against. Borrowed, so
     *        it has to outlive the view (it is the VoxelView's).
     * @param size_x, size_z: the area to cover from the terrain's origin, in
     *        voxels - the map's size.
     * @param shader_path: the shader files without their extension, e.g.
     *        "../resources/shaders/water" for water.vs and water.fs.
     *
     * Needs the window open: it uploads the mesh and compiles the shader.
     */
    WaterView(ViewNode* parent, const raylib::Camera* camera, int size_x, int size_z,
              const std::string& shader_path);
    ~WaterView() override;

    // Owns a mesh and a shader on the GPU
    WaterView(const WaterView&) = delete;
    WaterView& operator=(const WaterView&) = delete;

    std::string& get_view_type() override;
    void update(float delta_time) override;
    void render() override;

    /** Covers a size_x by size_z area from the terrain's origin instead, in voxels. */
    void set_area(int size_x, int size_z);

    /**
     * The sea floor where the voxel map holds nothing: flat squares at
     * `height` (the terrain's Y), drawn opaque in floor_colour before the
     * water, with the same mesh, shader and culling. main.cpp hands in one
     * per ocean cell, which the map leaves out (build_terrain_voxels()).
     */
    void set_floor(std::vector<WaterChunk> rects, float height);

    size_t chunk_count() const { return chunks.size(); }
    // How many chunks the last render() drew, i.e. passed the camera test
    size_t visible_chunk_count() const { return visible_last_frame; }

    /**
     * The matrix one chunk is drawn with: the shared mesh scaled to the chunk
     * and moved to its place at the surface, then the terrain's world matrix
     * on top. Plain maths, so the tests can check it.
     */
    static Matrix chunk_matrix(const WaterChunk& chunk, float surface_y, Matrix terrain);

private:
    const raylib::Camera* camera;
    std::vector<WaterChunk> chunks;
    std::vector<WaterChunk> floor_rects;
    float floor_height{0.0f};

    // One WATER_CHUNK_SIZE square, centred on the origin and cut into a quad
    // per unit (so a wave shader has vertices to move), shared by every chunk.
    // A chunk that is cut short at the edge is drawn scaled down to fit.
    Mesh chunk_mesh{};
    // Carries the water shader, which it owns: UnloadMaterial() unloads it.
    Material material{};

    int colour_loc{-1};
    int chunk_rect_loc{-1};
    int wave_time_loc{-1};
    int wave_amplitude_loc{-1};
    int wave_number_loc{-1};
    int wave_speed_loc{-1};

    // Frame time, not game time: the waves are cosmetic and keep moving while
    // the simulation is paused. Wrapped round once a wave period, which
    // changes nothing on screen and keeps the shader's sine accurate however
    // long the game runs.
    float wave_time{0.0f};

    size_t visible_last_frame{0};
};

#endif //BUSINESS_GAME_WATERVIEW_HPP
