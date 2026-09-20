//
// Created by Claude on 20.09.2026.
//

#include "TestHelpers.hpp"

#include "voxel/VoxelMesher.hpp"

/**
 * The CPU half of the mesher: which faces come out, and the ambient occlusion
 * baked into their vertex colours.
 *
 * build_chunk_mesh_data() is the half that needs no OpenGL context, which is
 * the whole reason it is split from the upload. Nothing here may call
 * build_chunk_mesh() or upload_chunk_mesh().
 */

namespace {

// A chunk is indexed x + y*CHUNK_SIZE + z*CHUNK_SIZE*CHUNK_SIZE
void set_voxel(VoxelChunk& chunk, const int x, const int y, const int z, const VoxelID id) {
    chunk[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] = id;
}

int vertex_count(const std::vector<MaterialMeshData>& data) {
    int count = 0;
    for (const auto& material : data) count += static_cast<int>(material.vertices.size() / 3);
    return count;
}

} // namespace

TEST_CASE("a face corner darkens as it is closed in", "[voxelmesh]") {
    REQUIRE(vertex_ao(false, false, false) == 3);   // open
    REQUIRE(vertex_ao(true, false, false) == 2);    // one side
    REQUIRE(vertex_ao(false, false, true) == 2);    // only the diagonal
    REQUIRE(vertex_ao(true, false, true) == 1);     // a side and the diagonal
    // Two sides shut the diagonal out, so it makes no difference either way
    REQUIRE(vertex_ao(true, true, false) == 0);
    REQUIRE(vertex_ao(true, true, true) == 0);
}

TEST_CASE("a lone voxel is six open faces", "[voxelmesh]") {
    VoxelChunk chunk{};
    set_voxel(chunk, 1, 1, 1, 1);

    const auto data = build_chunk_mesh_data(chunk, {}, Vector3{0, 0, 0}, 1.0f);

    REQUIRE(data.size() == 1);
    REQUIRE(data[0].id == 1);
    REQUIRE(vertex_count(data) == 24);              // 6 faces of 4 corners
    REQUIRE(data[0].indices.size() == 36);          // 2 triangles per face
    REQUIRE(data[0].colors.size() == 24 * 4);

    // Nothing is near it, so every corner is as open as it gets
    for (size_t i = 0; i < data[0].colors.size(); i += 4) {
        REQUIRE(data[0].colors[i] == 255);
    }
}

TEST_CASE("a face against the next chunk is left out", "[voxelmesh]") {
    // The hidden faces along a chunk border: the mesher used to treat anything
    // outside the chunk as air and emit a wall of faces no one can see.
    VoxelChunk chunk{};
    set_voxel(chunk, CHUNK_SIZE - 1, 1, 1, 1);

    SECTION("with no neighbour the outward face is drawn") {
        const auto data = build_chunk_mesh_data(chunk, {}, Vector3{0, 0, 0}, 1.0f);
        REQUIRE(vertex_count(data) == 24);
    }

    SECTION("a solid voxel in the next chunk drops it") {
        const auto neighbour = [](const int x, const int y, const int z) -> VoxelID {
            return (x == CHUNK_SIZE && y == 1 && z == 1) ? 1 : 0;
        };
        const auto data = build_chunk_mesh_data(chunk, neighbour, Vector3{0, 0, 0}, 1.0f);
        REQUIRE(vertex_count(data) == 20);          // five faces, not six
    }
}

TEST_CASE("a voxel diagonally above darkens the corners under it", "[voxelmesh]") {
    // The top face of the lower voxel, with a voxel sitting diagonally over one
    // of its edges: the two corners on that side are occluded, the two on the
    // far side are not.
    VoxelChunk chunk{};
    set_voxel(chunk, 1, 1, 1, 1);
    set_voxel(chunk, 2, 1, 2, 1);

    const auto data = build_chunk_mesh_data(chunk, {}, Vector3{0, 0, 0}, 1.0f);
    REQUIRE(data.size() == 1);
    const MaterialMeshData& mesh = data[0];

    // The top face of the lower voxel: pointing up, at the height of its top.
    // Found by what it is rather than by where it sits in the vertex list, so
    // the test does not care what order the faces come out in.
    int open = 0;
    int occluded = 0;
    for (size_t v = 0; v < mesh.vertices.size() / 3; ++v) {
        const float ny = mesh.normals[v * 3 + 1];
        const float y = mesh.vertices[v * 3 + 1];
        if (ny < 0.5f || y != Catch::Approx(2.0f).margin(test::EPS)) continue;

        if (mesh.colors[v * 4] == 255) {
            open++;
        } else {
            occluded++;
            // The dark ones are the corners on the side the voxel above is on
            REQUIRE(mesh.vertices[v * 3] == Catch::Approx(2.0f).margin(test::EPS));
        }
    }

    REQUIRE(occluded == 2);
    REQUIRE(open == 2);
}
