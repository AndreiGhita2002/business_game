//
// Created by Andrei Ghita on 08.09.2025.
//

#ifndef BUSINESS_GAME_VOXELMESHER_HPP
#define BUSINESS_GAME_VOXELMESHER_HPP
#include <functional>
#include <vector>

#include "VoxelMap.hpp"

/**
 * What sits just outside the chunk being meshed.
 *
 * The coordinates are chunk local and reach one voxel past each edge, so from
 * -1 to CHUNK_SIZE on every axis. Whatever answers has to look the voxel up in
 * the neighbouring chunk, or return 0 for air where there is nothing.
 *
 * An empty sampler is air everywhere outside, which is what a grid with no
 * neighbours (a SingleChunkGrid) wants.
 */
using VoxelNeighbourSampler = std::function<VoxelID(int x, int y, int z)>;

/** One material's mesh, before it goes to the GPU. */
struct MaterialMeshData {
    VoxelID id;
    std::vector<float> vertices;          // 3 per vertex
    std::vector<float> normals;           // 3 per vertex
    std::vector<float> uvs;               // 2 per vertex
    std::vector<unsigned char> colors;    // 4 per vertex, the baked AO shade
    std::vector<unsigned short> indices;  // 3 per triangle
};

struct MaterialMesh {
    VoxelID id;
    Mesh mesh;
};

/**
 * How dark one corner of a face is, from the three voxels around it: 3 is open
 * and 0 is boxed in.
 *
 * Two solid sides close the corner off whatever is diagonally behind them,
 * which is the rule that stops a lit seam running up the inside of a corner.
 */
int vertex_ao(bool side1, bool side2, bool corner);

/**
 * Builds a chunk's faces, with per-vertex ambient occlusion baked into the
 * vertex colours.
 *
 * CPU only - no OpenGL context is needed, which is what lets the tests cover
 * it. A face is left out where the neighbour is solid, the neighbouring chunk
 * included, so two chunks that meet no longer emit a wall of hidden faces
 * between them.
 */
std::vector<MaterialMeshData> build_chunk_mesh_data(
    const VoxelChunk& chunk,
    const VoxelNeighbourSampler& neighbour,
    Vector3 origin,
    float voxelSize);

/** Uploads what build_chunk_mesh_data() built. Needs an OpenGL context. */
std::vector<MaterialMesh> upload_chunk_mesh(const std::vector<MaterialMeshData>& data);

/** The two calls above, one after the other. */
std::vector<MaterialMesh> build_chunk_mesh(
    const VoxelChunk& chunk,
    const VoxelNeighbourSampler& neighbour,
    Vector3 origin,
    float voxelSize);

Model build_chunk_model(const std::vector<MaterialMesh>& mats, const std::map<VoxelID, Color>& voxelColourMap);

/**
 * Frees a model built by build_chunk_model() and leaves it empty.
 *
 * Always use this instead of UnloadModel(): raylib unloads the shader held by
 * every material it frees, and build_chunk_model() puts the shared voxel shader
 * on all of them, so a plain UnloadModel() takes the shader down with it and
 * every other voxel model stops drawing.
 */
void unload_chunk_model(Model& model);

#endif //BUSINESS_GAME_VOXELMESHER_HPP
