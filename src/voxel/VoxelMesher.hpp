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

/**
 * A chunk's mesh, before it goes to the GPU: one mesh for the whole chunk,
 * every colour in it. A vertex's colour is its voxel's colour from the palette
 * in rgb, and its ambient occlusion shade in alpha (lighting.fs reads them
 * that way), so a chunk is one draw call however many colours it has.
 */
struct ChunkMeshData {
    std::vector<float> vertices;          // 3 per vertex
    std::vector<float> normals;           // 3 per vertex
    std::vector<float> uvs;               // 2 per vertex
    std::vector<unsigned char> colors;    // 4 per vertex: palette rgb, AO shade in a
    std::vector<unsigned short> indices;  // 3 per triangle

    bool empty() const { return vertices.empty(); }
};

/**
 * How dark one corner of a face is, from the three voxels around it: 3 is open
 * and 0 is boxed in.
 *
 * Two solid sides close the corner off whatever is diagonally behind them,
 * which is the rule that stops a lit seam running up the inside of a corner.
 */
int vertex_ao(bool side1, bool side2, bool corner);

/** The colour a voxel id is drawn in: the palette's, or purple for an id it does not have. */
Color palette_colour(const std::map<VoxelID, Color>& palette, VoxelID id);

/**
 * Builds a chunk's faces, coloured from `palette`, with per-vertex ambient
 * occlusion baked into the vertex colours' alpha.
 *
 * CPU only - no OpenGL context is needed, which is what lets the tests cover
 * it. A face is left out where the neighbour is solid, the neighbouring chunk
 * included, so two chunks that meet no longer emit a wall of hidden faces
 * between them.
 *
 * **Greedy:** faces in the same plane, facing the same way, of the same
 * colour and with the same shade on all four corners are merged into one quad
 * as large a rectangle as they make. A face whose corners differ (the edge of
 * an occluded patch) is emitted on its own, so the shading is exactly what
 * one quad per face gave: a merged quad's corners all carry the one shade
 * every face in it had. Most of a landscape is open, evenly lit ground, which
 * comes down to a few quads per chunk.
 */
ChunkMeshData build_chunk_mesh_data(
    const VoxelChunk& chunk,
    const VoxelNeighbourSampler& neighbour,
    const std::map<VoxelID, Color>& palette,
    Vector3 origin,
    float voxelSize);

/**
 * Uploads what build_chunk_mesh_data() built. Needs an OpenGL context. An
 * empty mesh stays empty (no GPU buffers). Only the vertices and indices are
 * kept on the CPU afterwards.
 */
Mesh upload_chunk_mesh(const ChunkMeshData& data);

/** The two calls above, one after the other. */
Mesh build_chunk_mesh(
    const VoxelChunk& chunk,
    const VoxelNeighbourSampler& neighbour,
    const std::map<VoxelID, Color>& palette,
    Vector3 origin,
    float voxelSize);

/**
 * A model of the one mesh, with one material on the voxel shader. The
 * material is white: the colours are in the vertices. An empty mesh gives an
 * empty model, which allocates nothing.
 */
Model build_chunk_model(Mesh mesh);

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
