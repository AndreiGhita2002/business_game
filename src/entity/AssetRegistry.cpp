//
// Created by Andrei Ghita on 04.10.2026.
//

#include "entity/AssetRegistry.hpp"

#include <utility>

#include "voxel/SingleChunkGrid.hpp"
#include "voxel/VoxelFile.hpp"

AssetRegistry::AssetRegistry(VoxelColourMap palette) : palette(std::move(palette)) {}

void AssetRegistry::register_builder(const sim::AssetId& id, AssetBuilder build, const Vector3 pivot) {
    definitions[id] = Definition{"", std::move(build), pivot};
}

void AssetRegistry::register_file(const sim::AssetId& id, std::string path, const Vector3 pivot) {
    definitions[id] = Definition{std::move(path), {}, pivot};
}

bool AssetRegistry::contains(const sim::AssetId& id) const {
    return definitions.contains(id);
}

VoxelGrid* AssetRegistry::instantiate(const sim::AssetId& id, VoxelView* view, Vector3* out_pivot) const {
    const auto it = definitions.find(id);
    if (it == definitions.end()) return nullptr;
    const Definition& def = it->second;

    VoxelGrid* root = nullptr;
    if (!def.path.empty()) {
        root = voxel_file::load_grid(def.path, view, palette);
    } else if (def.build) {
        root = def.build(view, palette);
    }

    if (root != nullptr && out_pivot != nullptr) *out_pivot = def.pivot;
    return root;
}

// --- Placeholder car ---

namespace {

// Palette ids, from the map's colours in VoxelMap.cpp
constexpr VoxelID TYRE_COLOUR = 8;     // DARKGRAY
constexpr VoxelID WINDOW_COLOUR = 9;   // SKYBLUE

// Fills the box [from, to] inclusive
void fill(VoxelGrid* grid, const Int3 from, const Int3 to, const VoxelID id) {
    for (int x = from.x; x <= to.x; ++x)
        for (int y = from.y; y <= to.y; ++y)
            for (int z = from.z; z <= to.z; ++z)
                grid->set_voxel(Int3{x, y, z}, id);
}

} // namespace

AssetBuilder placeholder_car_builder(const VoxelID body_colour) {
    return [body_colour](VoxelView* view, const VoxelColourMap& palette) -> VoxelGrid* {
        // Grid coordinates, z up. The car runs along x and is 6 wide in y: the
        // body fills y 1 to 4 and the axle ends stick out to y 0 and y 5.
        auto* body = new SingleChunkGrid(view, palette);
        body->name = "car body";
        fill(body, Int3{0, 1, 1}, Int3{7, 4, 2}, body_colour);
        fill(body, Int3{2, 1, 3}, Int3{5, 4, 3}, WINDOW_COLOUR);
        fill(body, Int3{2, 1, 4}, Int3{5, 4, 4}, body_colour);

        const Int3 axle_ends[4] = {{1, 0, 1}, {6, 0, 1}, {1, 5, 1}, {6, 5, 1}};
        for (const Int3& axle : axle_ends) body->set_voxel(axle, TYRE_COLOUR);

        for (const Int3& axle : axle_ends) {
            // A plus shape in the grid's x/z plane, so its turning shows. The
            // hub is the connector: attaching puts it exactly on the axle end,
            // so the two are the same colour or the shared cube would flicker.
            auto* wheel = new SingleChunkGrid(view, palette);
            wheel->name = WHEEL_GRID_NAME;
            const Int3 hub{1, 0, 1};
            for (const Int3& v : {Int3{1, 0, 0}, Int3{0, 0, 1}, hub, Int3{2, 0, 1}, Int3{1, 0, 2}}) {
                wheel->set_voxel(v, TYRE_COLOUR);
            }
            wheel->attach_to(body, axle, hub);
        }
        return body;
    };
}
