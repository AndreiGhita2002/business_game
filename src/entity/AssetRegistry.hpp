//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_ASSETREGISTRY_HPP
#define BUSINESS_GAME_ASSETREGISTRY_HPP
#include <functional>
#include <map>
#include <string>

#include <raylib.h>

#include "sim/Vehicles.hpp"
#include "voxel/VoxelGrid.hpp"

class VoxelView;

/** Builds a fresh grid tree in code, returning its root, owned by the caller. */
using AssetBuilder = std::function<VoxelGrid*(VoxelView* view, const VoxelColourMap& palette)>;

/**
 * Turns the names the simulation uses for how things look (sim::AssetId) into
 * voxel grids. This is where the simulation's "car" becomes something to draw,
 * and the only place that knows what a name means visually.
 *
 * An asset is either a .bgvox file or a function that builds the grids in
 * code. resources/grids/ is still empty and there is no save UI, so for now
 * every asset is built in code; a file registered under the same name later
 * replaces it without anything else changing.
 */
class AssetRegistry {
public:
    /** @param palette: the colours every instantiated grid is put on. */
    explicit AssetRegistry(VoxelColourMap palette);

    /**
     * @param pivot: the point of the model, in its root grid's model space,
     *        that is placed on the simulation's position. For a vehicle that
     *        is the middle of its footprint, at the bottom.
     */
    void register_builder(const sim::AssetId& id, AssetBuilder build, Vector3 pivot);
    void register_file(const sim::AssetId& id, std::string path, Vector3 pivot);

    bool contains(const sim::AssetId& id) const;

    /**
     * A new copy of an asset's grids. Returns the root, owned by the caller
     * with every grid below it, and its pivot in `out_pivot`. Nothing when the
     * name is unknown or the file does not load.
     */
    VoxelGrid* instantiate(const sim::AssetId& id, VoxelView* view, Vector3* out_pivot) const;

private:
    struct Definition {
        std::string path;
        AssetBuilder build;
        Vector3 pivot;
    };

    VoxelColourMap palette;
    std::map<sim::AssetId, Definition> definitions;
};

// --- Placeholder assets, until there are files ---

// What a car's wheels are called, which is how WheelSpinScript finds them in
// whatever grid tree an asset produced, built or loaded
#define WHEEL_GRID_NAME "wheel"

// The wheels' radius in voxels, from the middle of the hub to the outer edge,
// which is what touches the ground and so what sets how fast a wheel turns
constexpr float PLACEHOLDER_WHEEL_RADIUS = 1.5f;

/** The middle of the placeholder car's footprint, at the bottom of its wheels. */
constexpr Vector3 PLACEHOLDER_CAR_PIVOT{4.0f, 0.0f, 3.0f};

/**
 * A builder for a small car: an 8 by 6 voxel body and cabin in `body_colour`
 * (an id in the scene's palette), with four wheel grids attached at the axle
 * ends, each named WHEEL_GRID_NAME. The car faces +X.
 */
AssetBuilder placeholder_car_builder(VoxelID body_colour);

#endif //BUSINESS_GAME_ASSETREGISTRY_HPP
