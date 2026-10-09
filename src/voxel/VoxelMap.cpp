//
// Created by Andrei Ghita on 01.09.2025.
//

#include "voxel/VoxelMap.hpp"

#include <raylib-cpp.hpp>
#include <algorithm>
#include <cstring>
#include <istream>
#include <ostream>

#include "voxel/VoxelMesher.hpp"
#include "voxel/VoxelVolume.hpp"
#include "game/main.hpp"

VoxelMap::VoxelMap(VoxelView* view, const uint32_t size_x, const uint32_t size_y, const uint32_t size_z)
    : VoxelGrid(view)
{
    resize(size_x, size_y, size_z);

    this->transform = identity();

    //TODO (optimisation) colorMaps should be shared between grids, somewhere global
    this->voxel_colours = std::make_shared<std::map<VoxelID, Color>>();
    auto colorMap = this->voxel_colours.get();
    colorMap->insert(std::pair<VoxelID, Color>(0, RED)); // air, should not be seen
    colorMap->insert(std::pair<VoxelID, Color>(1, BEIGE));
    // Grass, see GRASS_VOXEL in entity/TerrainVoxels.hpp
    colorMap->insert(std::pair<VoxelID, Color>(2, DARKGREEN));
    colorMap->insert(std::pair<VoxelID, Color>(3, YELLOW));
    // Not used by the terrain, except where marked, these are here to fill
    // out the editor palette
    colorMap->insert(std::pair<VoxelID, Color>(4, BLUE));
    colorMap->insert(std::pair<VoxelID, Color>(5, ORANGE));
    colorMap->insert(std::pair<VoxelID, Color>(6, PURPLE));
    // Dirt, DIRT_VOXEL
    colorMap->insert(std::pair<VoxelID, Color>(7, BROWN));
    colorMap->insert(std::pair<VoxelID, Color>(8, DARKGRAY));
    colorMap->insert(std::pair<VoxelID, Color>(9, SKYBLUE));
    colorMap->insert(std::pair<VoxelID, Color>(10, MAROON));
    colorMap->insert(std::pair<VoxelID, Color>(11, RAYWHITE));
    // The voxel a grid made by the editor's "New Grid" button starts with, see
    // NEW_GRID_VOXEL_ID
    colorMap->insert(std::pair<VoxelID, Color>(12, BLACK));
    // Stone, STONE_VOXEL
    colorMap->insert(std::pair<VoxelID, Color>(13, GRAY));
    // The islands' other blocks, see entity/TerrainVoxels.hpp
    colorMap->insert(std::pair<VoxelID, Color>(14, Color{222, 200, 140, 255}));  // sand
    colorMap->insert(std::pair<VoxelID, Color>(15, Color{196, 150, 96, 255}));   // sandstone
    colorMap->insert(std::pair<VoxelID, Color>(16, Color{240, 244, 250, 255}));  // snow
    colorMap->insert(std::pair<VoxelID, Color>(17, Color{122, 118, 112, 255}));  // gravel

    // No chunks: they are made as they are first written to
}

VoxelMap::~VoxelMap() {
    clear();
}

VoxelChunk& VoxelMap::ensure_chunk(const Int3 chunk_pos) {
    const auto found = chunks.find(chunk_pos);
    if (found != chunks.end()) return found->second;

    VoxelChunk& chunk = chunks[chunk_pos];
    chunk.fill(0);
    chunk_was_updated[chunk_pos] = true;
    chunk_volume_dirty[chunk_pos] = true;
    // Its neighbours' meshes were made with air where it is, which is still
    // right until something solid is written into it, and that write marks
    // them itself (write_voxel()). Something writing straight into the chunk
    // instead marks what it needs.
    return chunk;
}

void VoxelMap::clear() {
    for (auto it = chunk_models.begin(); it != chunk_models.end(); ++it) {
        unload_chunk_model(it->second.model);
    }
    chunk_models.clear();
    chunks.clear();
    chunk_was_updated.clear();
    chunk_volume_dirty.clear();
}

void VoxelMap::mark_for_remesh(const Int3 lo, const Int3 hi) {
    // Walked over the chunks rather than over the box, which may be most of
    // the map tall and is mostly chunks that do not exist
    for (const auto& [chunk_pos, chunk] : chunks) {
        if (chunk_pos.x < lo.x || chunk_pos.y < lo.y || chunk_pos.z < lo.z ||
            chunk_pos.x > hi.x || chunk_pos.y > hi.y || chunk_pos.z > hi.z)
            continue;
        chunk_was_updated[chunk_pos] = true;
        chunk_volume_dirty[chunk_pos] = true;
    }
}

void VoxelMap::resize(const uint32_t size_x, const uint32_t size_y, const uint32_t size_z) {
    clear();
    this->size = Int2(size_x, size_y);
    this->height = static_cast<int>(size_z);
    this->chunk_count = Int3(
        size_x / CHUNK_SIZE + (size_x % CHUNK_SIZE ? 1 : 0),
        size_y / CHUNK_SIZE + (size_y % CHUNK_SIZE ? 1 : 0),
        size_z / CHUNK_SIZE + (size_z % CHUNK_SIZE ? 1 : 0));
}

std::string & VoxelMap::get_grid_type() {
    static std::string TYPE = VOXEL_MAP_STR;
    return TYPE;
}

bool VoxelMap::write_body(std::ostream& out) {
    voxel_file::write_i32(out, size.x);
    voxel_file::write_i32(out, size.y);
    voxel_file::write_i32(out, height);
    voxel_file::write_u32(out, static_cast<uint32_t>(chunks.size()));

    for (const auto& [chunk_pos, chunk] : chunks) {
        voxel_file::write_i32(out, chunk_pos.x);
        voxel_file::write_i32(out, chunk_pos.y);
        voxel_file::write_i32(out, chunk_pos.z);
        // The voxels go out in the shared format, the same one a single chunk
        // grid uses, so only the chunk grid around them is particular to a map
        if (!voxel_file::write_chunk(out, chunk)) return false;
    }
    return out.good();
}

VoxelGrid* VoxelMap::load_body(std::istream& in, const voxel_file::LoadContext& ctx) {
    int32_t size_x = 0, size_y = 0, size_z = 0;
    uint32_t chunk_count = 0;
    if (!voxel_file::read_i32(in, &size_x) ||
        !voxel_file::read_i32(in, &size_y) ||
        !voxel_file::read_i32(in, &size_z) ||
        !voxel_file::read_u32(in, &chunk_count))
        return nullptr;

    // Capped so a corrupt size cannot ask for gigabytes of chunks
    if (size_x <= 0 || size_y <= 0 || size_z <= 0 ||
        size_x > MAX_MAP_SIZE || size_y > MAX_MAP_SIZE || size_z > MAX_MAP_SIZE) {
        TraceLog(LOG_WARNING, "VOXELMAP: file asks for a %i by %i by %i map", size_x, size_y, size_z);
        return nullptr;
    }
    // A corrupt count would otherwise send the loop below allocating chunks
    // until the read finally fails
    const uint32_t chunks_x = size_x / CHUNK_SIZE + (size_x % CHUNK_SIZE ? 1 : 0);
    const uint32_t chunks_y = size_y / CHUNK_SIZE + (size_y % CHUNK_SIZE ? 1 : 0);
    const uint32_t chunks_z = size_z / CHUNK_SIZE + (size_z % CHUNK_SIZE ? 1 : 0);
    if (chunk_count > chunks_x * chunks_y * chunks_z) {
        TraceLog(LOG_WARNING, "VOXELMAP: file holds %u chunks, a %i by %i by %i map has room for %u",
                 chunk_count, size_x, size_y, size_z, chunks_x * chunks_y * chunks_z);
        return nullptr;
    }

    // The chunks are read into a map that already holds air, so a file that
    // leaves some of them out still gives a complete grid
    auto* map = new VoxelMap(ctx.view, static_cast<uint32_t>(size_x), static_cast<uint32_t>(size_y),
                             static_cast<uint32_t>(size_z));
    if (ctx.palette) map->voxel_colours = ctx.palette;

    for (uint32_t i = 0; i < chunk_count; ++i) {
        int32_t chunk_x = 0, chunk_y = 0, chunk_z = 0;
        if (!voxel_file::read_i32(in, &chunk_x) || !voxel_file::read_i32(in, &chunk_y) ||
            !voxel_file::read_i32(in, &chunk_z)) {
            delete map;
            return nullptr;
        }
        // Only chunks inside the map: one outside it would be meshed and
        // drawn, but never reachable through get_voxel()
        const Int3 chunk_pos{chunk_x, chunk_y, chunk_z};
        const Int3 count = map->get_chunk_count();
        if (chunk_x < 0 || chunk_x >= count.x || chunk_y < 0 || chunk_y >= count.y ||
            chunk_z < 0 || chunk_z >= count.z) {
            TraceLog(LOG_WARNING, "VOXELMAP: file holds chunk %i %i %i, outside the map",
                     chunk_x, chunk_y, chunk_z);
            delete map;
            return nullptr;
        }
        if (!voxel_file::read_chunk(in, &map->ensure_chunk(chunk_pos))) {
            delete map;
            return nullptr;
        }
        map->chunk_was_updated[chunk_pos] = true;
        map->chunk_volume_dirty[chunk_pos] = true;
    }
    return map;
}

namespace {

// Which of a chunk's 3x3x3 block of neighbours (itself in the middle) a chunk
// local coordinate one voxel either side of the chunk falls in, on one axis
int neighbour_side(const int v) {
    return v < 0 ? 0 : (v >= CHUNK_SIZE ? 2 : 1);
}

int around_index(const int sx, const int sy, const int sz) {
    return sx + sy * 3 + sz * 9;
}

/** Whether every voxel of `chunk` in layer `layer` across `axis` (0 x, 1 y, 2 z) is solid. */
bool plane_solid(const VoxelChunk& chunk, const int axis, const int layer) {
    for (int a = 0; a < CHUNK_SIZE; ++a) {
        for (int b = 0; b < CHUNK_SIZE; ++b) {
            const int x = axis == 0 ? layer : a;
            const int y = axis == 1 ? layer : (axis == 0 ? a : b);
            const int z = axis == 2 ? layer : b;
            if (chunk[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] == 0) return false;
        }
    }
    return true;
}

} // namespace

void VoxelMap::update_models() {
    // The render distance, only when it is limited. A question about the
    // world, so each chunk is carried out of the map's space first; the map's
    // own place is the same for every chunk, so it is only worked out once.
    if (global::limit_render_distance) {
        const Transform world = get_world_transform();
        for (auto& [chunk_pos, info] : chunk_models) {
            const Transform chunk_world = transform_transform(info.transform, world);
            info.do_render = view->isInRenderDistance(chunk_world.translation);
        }
    }

    // Only the chunks marked for it. Walking the flags is far cheaper than
    // looking every chunk up every frame, as almost none are marked.
    mesh_queue.clear();
    for (const auto& [chunk_pos, dirty] : chunk_was_updated) {
        if (dirty) mesh_queue.push_back(chunk_pos);
    }
    if (mesh_queue.empty()) return;

    // A new island marks thousands at once, more than one frame can mesh
    // without a stall, so a frame meshes what MESH_BUDGET_SECONDS allows and
    // leaves the rest marked for the next. Nearest the camera first, so the
    // land in view fills in before what is behind it. An edit marks a few,
    // which all go in the frame it was made.
    if (mesh_queue.size() > 1 && view != nullptr) {
        // The camera in the map's own space (X grid x, Y grid z, Z grid y),
        // where the chunks are
        const Vector3 eye = Vector3Transform(view->camera.position,
                                             MatrixInvert(transform_to_matrix(get_world_transform())));
        const auto distance_sq = [&eye](const Int3 c) {
            const float half = CHUNK_SIZE * 0.5f;
            const float dx = static_cast<float>(c.x * CHUNK_SIZE) + half - eye.x;
            const float dy = static_cast<float>(c.z * CHUNK_SIZE) + half - eye.y;
            const float dz = static_cast<float>(c.y * CHUNK_SIZE) + half - eye.z;
            return dx * dx + dy * dy + dz * dz;
        };
        std::sort(mesh_queue.begin(), mesh_queue.end(), [&distance_sq](const Int3 a, const Int3 b) {
            return distance_sq(a) < distance_sq(b);
        });
    }
    const double deadline = GetTime() + MESH_BUDGET_SECONDS;

    for (size_t queued = 0; queued < mesh_queue.size(); ++queued) {
        // At least one a frame, however slow
        if (queued > 0 && GetTime() > deadline) break;
        const Int3 chunk_pos = mesh_queue[queued];
        chunk_was_updated[chunk_pos] = false;

        const auto found = chunks.find(chunk_pos);
        if (found == chunks.end()) continue;
        const VoxelChunk& chunk = found->second;

        // Where the chunk sits inside the map. Chunk (cx, cy, cz) holds the
        // global voxels 16cx to 16cx+15 (and the same on y and z), and a voxel
        // spans one unit, so chunks sit
        // CHUNK_SIZE apart and meet exactly. A smaller spacing would overlap
        // them and draw two different columns of terrain in the same place.
        // What the map is doing is left out of this on purpose: it is applied
        // on top by voxel_model_matrix() when the chunk is drawn, so that a
        // moved map does not need remeshing.
        auto model_transform = identity();
        model_transform.translation = Vector3{
            static_cast<float>(chunk_pos.x) * CHUNK_SIZE,
            static_cast<float>(chunk_pos.z) * CHUNK_SIZE,
            static_cast<float>(chunk_pos.y) * CHUNK_SIZE
        };

        // The chunk and its 26 neighbours, looked up once rather than once
        // for every voxel the mesher reads past the chunk's edge. A chunk that
        // does not exist is air. Chunks only exist inside the map, so this
        // needs no bounds check of its own.
        const VoxelChunk* around[27];
        for (int sz = 0; sz < 3; ++sz) {
            for (int sy = 0; sy < 3; ++sy) {
                for (int sx = 0; sx < 3; ++sx) {
                    const auto n = chunks.find(Int3{chunk_pos.x + sx - 1, chunk_pos.y + sy - 1, chunk_pos.z + sz - 1});
                    around[around_index(sx, sy, sz)] = n != chunks.end() ? &n->second : nullptr;
                }
            }
        }
        const int base_z = chunk_pos.z * CHUNK_SIZE;

        // What sits just outside this chunk. The mesher reads it to leave out
        // the faces between two chunks that meet, and to work out the ambient
        // occlusion of a corner on the border. The coordinates are chunk local
        // and reach one voxel past each edge. Below the map counts as solid:
        // nothing can see the map's underside, and meshing it would be a face
        // under every column.
        const auto neighbour = [&around, base_z](const int x, const int y, const int z) -> VoxelID {
            if (base_z + z < 0) return 1;
            const int sx = neighbour_side(x), sy = neighbour_side(y), sz = neighbour_side(z);
            const VoxelChunk* c = around[around_index(sx, sy, sz)];
            if (c == nullptr) return 0;
            const int lx = x - (sx - 1) * CHUNK_SIZE;
            const int ly = y - (sy - 1) * CHUNK_SIZE;
            const int lz = z - (sz - 1) * CHUNK_SIZE;
            return (*c)[lx + ly * CHUNK_SIZE + lz * CHUNK_SIZE * CHUNK_SIZE];
        };

        // A chunk that is solid all through with solid on all six sides has
        // no face to show, which is most of an island: its insides. Found
        // without the mesher, which would read every voxel six times over to
        // arrive at the same empty model.
        const auto side_solid = [&around, base_z](const int sx, const int sy, const int sz, const int axis,
                                                  const int layer) {
            if (sz == 0 && base_z == 0) return true;   // the floor of the map
            const VoxelChunk* c = around[around_index(sx, sy, sz)];
            return c != nullptr && plane_solid(*c, axis, layer);
        };
        const bool buried =
            std::find(chunk.begin(), chunk.end(), VoxelID{0}) == chunk.end() &&
            side_solid(2, 1, 1, 0, 0) && side_solid(0, 1, 1, 0, CHUNK_SIZE - 1) &&
            side_solid(1, 2, 1, 1, 0) && side_solid(1, 0, 1, 1, CHUNK_SIZE - 1) &&
            side_solid(1, 1, 2, 2, 0) && side_solid(1, 1, 0, 2, CHUNK_SIZE - 1);

        Model new_model = build_chunk_model(Mesh{});
        BoundingBox bounds{};
        if (!buried) {
            const ChunkMeshData data = build_chunk_mesh_data(chunk, neighbour, *voxel_colours, Vector3{0.0, 0.0, 0.0}, 1.0f);
            bounds = chunk_mesh_bounds(data);
            new_model = build_chunk_model(upload_chunk_mesh(data));
        }

        // A chunk that is meshed again already holds a model, which would
        // leak its GPU buffers if it were simply overwritten. This happens
        // on every voxel the editor places.
        const auto chunk_model = chunk_models.find(chunk_pos);
        if (chunk_model != chunk_models.end()) unload_chunk_model(chunk_model->second.model);

        chunk_models[chunk_pos] = ModelInfo{true, new_model, model_transform, bounds};
    }
}

bool VoxelMap::update_volume(VoxelVolume& volume, VoxelVolume* coarse, const Int3 window_origin) {
    if (!volume.is_created()) return false;

    const Int3 window_size = volume.get_size();
    bool uploaded = false;
    for (auto& [chunk_pos, dirty] : chunk_volume_dirty) {
        if (!dirty) continue;
        dirty = false;

        const auto chunk = chunks.find(chunk_pos);
        if (chunk == chunks.end()) continue;

        // Chunk (cx, cy, cz) starts at voxel 16 * (cx, cy, cz), which is where
        // update_models() meshes it, and the window moves that back by its
        // origin. The map's own transform is not baked in here either: the
        // shader undoes it when it turns a world position into a voxel
        // coordinate.
        const Int3 at{
            chunk_pos.x * CHUNK_SIZE - window_origin.x,
            chunk_pos.y * CHUNK_SIZE - window_origin.y,
            chunk_pos.z * CHUNK_SIZE - window_origin.z,
        };
        if (at.x < 0 || at.y < 0 || at.z < 0 || at.x + CHUNK_SIZE > window_size.x ||
            at.y + CHUNK_SIZE > window_size.y || at.z + CHUNK_SIZE > window_size.z)
            continue;

        volume.upload_chunk(at, chunk->second);
        // And the chunk's cells of the coarse volume, which may have emptied
        // or filled with it
        if (coarse != nullptr && coarse->is_created()) {
            VoxelID cells[CHUNK_COARSE * CHUNK_COARSE * CHUNK_COARSE];
            coarse_cells(chunk->second, cells);
            coarse->upload_block(Int3{at.x / WORLD_COARSE, at.y / WORLD_COARSE, at.z / WORLD_COARSE},
                                 Int3{CHUNK_COARSE, CHUNK_COARSE, CHUNK_COARSE}, cells);
        }
        uploaded = true;
    }
    return uploaded;
}

void VoxelMap::copy_window(VoxelID* out, const Int3 origin, const Int3 size) {
    for (auto& [chunk_pos, dirty] : chunk_volume_dirty) dirty = false;

    const size_t row = static_cast<size_t>(size.x);
    const size_t layer = row * static_cast<size_t>(size.y);
    for (const auto& [chunk_pos, chunk] : chunks) {
        const Int3 at{
            chunk_pos.x * CHUNK_SIZE - origin.x,
            chunk_pos.y * CHUNK_SIZE - origin.y,
            chunk_pos.z * CHUNK_SIZE - origin.z,
        };
        if (at.x < 0 || at.y < 0 || at.z < 0 || at.x + CHUNK_SIZE > size.x ||
            at.y + CHUNK_SIZE > size.y || at.z + CHUNK_SIZE > size.z)
            continue;

        // A chunk's rows run along x as the window's do, so each goes over whole
        for (int z = 0; z < CHUNK_SIZE; ++z) {
            for (int y = 0; y < CHUNK_SIZE; ++y) {
                std::memcpy(out + static_cast<size_t>(at.x) + static_cast<size_t>(at.y + y) * row +
                                static_cast<size_t>(at.z + z) * layer,
                            chunk.data() + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE, CHUNK_SIZE);
            }
        }
    }
}

int VoxelMap::solid_top() const {
    // One pass over the chunks, each read from its top layer down and only as
    // far as could still beat the best found so far: a chunk wholly below it
    // is not read at all, which is most of them
    int best = 0;
    for (const auto& [chunk_pos, chunk] : chunks) {
        const int chunk_bottom = chunk_pos.z * CHUNK_SIZE;
        for (int in_chunk_z = CHUNK_SIZE - 1; in_chunk_z >= 0; --in_chunk_z) {
            const int top = chunk_bottom + in_chunk_z + 1;
            if (top <= best) break;
            const auto first = chunk.begin() + in_chunk_z * CHUNK_SIZE * CHUNK_SIZE;
            const auto last = first + CHUNK_SIZE * CHUNK_SIZE;
            if (std::any_of(first, last, [](const VoxelID v) { return v != 0; })) {
                best = top;
                break;
            }
        }
    }
    return std::min(best, height);
}

std::vector<ModelInfo*> VoxelMap::get_models() {
    auto out = std::vector<ModelInfo*>{};
    out.reserve(chunk_models.size());
    for (auto it = chunk_models.begin(); it != chunk_models.end(); ++it) {
        // A chunk of nothing but air, or buried under its neighbours, has no
        // faces. A tall map is mostly those, and each would otherwise still
        // cost a draw call and a pick of its shadow casters.
        if (it->second.do_render && it->second.model.meshCount > 0) {
            out.emplace_back(&it->second);
        }
    }
    return out;
}

bool VoxelMap::in_bounds(const Int3 grid_pos) const {
    // get_voxel() wraps an out of range coordinate into a chunk instead of
    // rejecting it, so everything that reaches it comes through here first
    return grid_pos.x >= 0 && grid_pos.x < size.x &&
           grid_pos.y >= 0 && grid_pos.y < size.y &&
           grid_pos.z >= 0 && grid_pos.z < height;
}

bool VoxelMap::write_voxel(const Int3 grid_pos, const VoxelID id) {
    if (!in_bounds(grid_pos)) return false;

    const Int3 chunk_pos{
        floordiv(grid_pos.x, CHUNK_SIZE),
        floordiv(grid_pos.y, CHUNK_SIZE),
        floordiv(grid_pos.z, CHUNK_SIZE)
    };
    // Air into a chunk that does not exist is already there
    if (id == 0 && chunks.find(chunk_pos) == chunks.end()) return true;

    VoxelChunk& chunk = ensure_chunk(chunk_pos);
    *get_chunk_voxel(chunk, Int3{
        floormod(grid_pos.x, CHUNK_SIZE),
        floormod(grid_pos.y, CHUNK_SIZE),
        floormod(grid_pos.z, CHUNK_SIZE),
    }) = id;
    // The chunk's mesh and its place in the shadow volume are both a voxel out
    // of date now
    chunk_was_updated[chunk_pos] = true;
    chunk_volume_dirty[chunk_pos] = true;

    // A voxel on the edge of a chunk shows up in its neighbour's mesh too, now
    // that the mesher reads across the border for hidden faces and for the
    // ambient occlusion of a corner. The diagonals count as well, as an AO
    // corner reads them. Only the mesh: the volume holds each chunk on its own.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                const Int3 other{
                    floordiv(grid_pos.x + dx, CHUNK_SIZE),
                    floordiv(grid_pos.y + dy, CHUNK_SIZE),
                    floordiv(grid_pos.z + dz, CHUNK_SIZE)
                };
                if (other == chunk_pos) continue;
                if (chunks.find(other) != chunks.end()) chunk_was_updated[other] = true;
            }
        }
    }
    return true;
}

ModelInfo* VoxelMap::model_for_voxel(const Int3 grid_pos) {
    if (!in_bounds(grid_pos)) return nullptr;
    const auto found = chunk_models.find(Int3{
        floordiv(grid_pos.x, CHUNK_SIZE), floordiv(grid_pos.y, CHUNK_SIZE), floordiv(grid_pos.z, CHUNK_SIZE)});
    return found != chunk_models.end() ? &found->second : nullptr;
}

bool VoxelMap::model_to_grid(const ModelInfo* model, const Vector3 local_pos, Int3* out) {
    if (model == nullptr) return false;

    // Each chunk is meshed at its own origin, so the local position only gives
    // the voxel inside the chunk. Which chunk it is comes from the offset the
    // model was placed at (update_models(), in model space: x, z, y), checked
    // against the map so a model from somewhere else is refused.
    const Vector3 offset = model->transform.translation;
    const Int3 chunk_pos{
        static_cast<int>(floorf(offset.x / CHUNK_SIZE + 0.5f)),
        static_cast<int>(floorf(offset.z / CHUNK_SIZE + 0.5f)),
        static_cast<int>(floorf(offset.y / CHUNK_SIZE + 0.5f)),
    };
    const auto found = chunk_models.find(chunk_pos);
    if (found == chunk_models.end() || &found->second != model) return false;

    // Model space is (x, z, y) in grid terms
    *out = Int3{
        chunk_pos.x * CHUNK_SIZE + static_cast<int>(floorf(local_pos.x)),
        chunk_pos.y * CHUNK_SIZE + static_cast<int>(floorf(local_pos.z)),
        chunk_pos.z * CHUNK_SIZE + static_cast<int>(floorf(local_pos.y)),
    };
    return true;
}

VoxelChunk* VoxelMap::get_chunk(const Int3 pos) {
    // finding the chunk
    const int cx = floordiv(pos.x, CHUNK_SIZE);
    const int cy = floordiv(pos.y, CHUNK_SIZE);
    const int cz = floordiv(pos.z, CHUNK_SIZE);

    auto pair = chunks.find(Int3{cx, cy, cz});

    if (pair == chunks.end()) return nullptr;
    else return &pair->second;
}

VoxelID* VoxelMap::get_voxel(Int3 pos) {
    // finding the chunk
    auto chunk = get_chunk(pos);
    if (chunk == nullptr) return nullptr;

    // getting the voxel inside the chunk
    Int3 chunk_pos = {
        floormod(pos.x, CHUNK_SIZE),
        floormod(pos.y, CHUNK_SIZE),
        floormod(pos.z, CHUNK_SIZE),
    };
    return get_chunk_voxel(*chunk, chunk_pos);
}

VoxelID* VoxelMap::get_chunk_voxel(VoxelChunk& chunk, const Int3 pos) {
    return &chunk[pos.x
        + pos.y * CHUNK_SIZE
        + pos.z * CHUNK_SIZE * CHUNK_SIZE];
}

Int2 VoxelMap::get_size() {
    return size;
}

int VoxelMap::get_height() const {
    return height;
}

Int3 VoxelMap::get_chunk_count() const {
    return chunk_count;
}
