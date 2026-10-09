//
// Created by Claude on 20.09.2026.
//

#include "TestHelpers.hpp"

#include <cmath>

#include "sim/Rng.hpp"
#include "voxel/VoxelMesher.hpp"

/**
 * The CPU half of the mesher: which faces come out, the colours and ambient
 * occlusion baked into their vertex colours, and the greedy merging of faces
 * into larger quads.
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

const std::map<VoxelID, Color>& palette() {
    static const std::map<VoxelID, Color> colours = *test::make_palette();
    return colours;
}

ChunkMeshData mesh(const VoxelChunk& chunk, const VoxelNeighbourSampler& neighbour = {}) {
    return build_chunk_mesh_data(chunk, neighbour, palette(), Vector3{0, 0, 0}, 1.0f);
}

int vertex_count(const ChunkMeshData& data) {
    return static_cast<int>(data.vertices.size() / 3);
}

Vector3 vertex(const ChunkMeshData& data, const size_t v) {
    return Vector3{data.vertices[v * 3], data.vertices[v * 3 + 1], data.vertices[v * 3 + 2]};
}

Vector3 normal(const ChunkMeshData& data, const size_t v) {
    return Vector3{data.normals[v * 3], data.normals[v * 3 + 1], data.normals[v * 3 + 2]};
}

/**
 * The area of every quad facing along `n`, in faces: what a merged mesh covers
 * should be what one quad per face would have, face for face.
 */
float area_facing(const ChunkMeshData& data, const Vector3 n) {
    float area = 0.0f;
    for (size_t t = 0; t < data.indices.size(); t += 3) {
        const size_t a = data.indices[t], b = data.indices[t + 1], c = data.indices[t + 2];
        if (Vector3DotProduct(normal(data, a), n) < 0.5f) continue;
        const Vector3 cross = Vector3CrossProduct(Vector3Subtract(vertex(data, b), vertex(data, a)),
                                                  Vector3Subtract(vertex(data, c), vertex(data, a)));
        area += 0.5f * Vector3Length(cross);
    }
    return area;
}

// The six face directions in the mesh's (world) axes, and the same in the
// chunk's: world X is x, world Y is z (up), world Z is y
constexpr int DIRECTIONS[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
Vector3 world_normal(const int d) {
    return Vector3{static_cast<float>(DIRECTIONS[d][0]), static_cast<float>(DIRECTIONS[d][2]),
                   static_cast<float>(DIRECTIONS[d][1])};
}

/** How many faces point along chunk direction d, counted one face at a time. */
int exposed_faces(const VoxelChunk& chunk, const int d) {
    int count = 0;
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                if (chunk[x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE] == 0) continue;
                const int nx = x + DIRECTIONS[d][0], ny = y + DIRECTIONS[d][1], nz = z + DIRECTIONS[d][2];
                const bool inside = nx >= 0 && nx < CHUNK_SIZE && ny >= 0 && ny < CHUNK_SIZE &&
                                    nz >= 0 && nz < CHUNK_SIZE;
                if (!inside || chunk[nx + ny * CHUNK_SIZE + nz * CHUNK_SIZE * CHUNK_SIZE] == 0) count++;
            }
        }
    }
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

TEST_CASE("a lone voxel is six open faces in its palette colour", "[voxelmesh]") {
    VoxelChunk chunk{};
    set_voxel(chunk, 1, 1, 1, 1);

    const ChunkMeshData data = mesh(chunk);

    REQUIRE(vertex_count(data) == 24);              // 6 faces of 4 corners
    REQUIRE(data.indices.size() == 36);             // 2 triangles per face
    REQUIRE(data.colors.size() == 24 * 4);

    // The colour is the palette's, and nothing is near it, so every corner is
    // as open as it gets
    const Color red = palette().at(1);
    for (size_t i = 0; i < data.colors.size(); i += 4) {
        REQUIRE(data.colors[i] == red.r);
        REQUIRE(data.colors[i + 1] == red.g);
        REQUIRE(data.colors[i + 2] == red.b);
        REQUIRE(data.colors[i + 3] == 255);
    }
}

TEST_CASE("an id the palette does not have is drawn purple", "[voxelmesh]") {
    VoxelChunk chunk{};
    set_voxel(chunk, 0, 0, 0, 200);
    const ChunkMeshData data = mesh(chunk);
    REQUIRE(data.colors[0] == PURPLE.r);
    REQUIRE(data.colors[1] == PURPLE.g);
    REQUIRE(data.colors[2] == PURPLE.b);
}

TEST_CASE("a face against the next chunk is left out", "[voxelmesh]") {
    // The hidden faces along a chunk border: the mesher used to treat anything
    // outside the chunk as air and emit a wall of faces no one can see.
    VoxelChunk chunk{};
    set_voxel(chunk, CHUNK_SIZE - 1, 1, 1, 1);

    SECTION("with no neighbour the outward face is drawn") {
        REQUIRE(vertex_count(mesh(chunk)) == 24);
    }

    SECTION("a solid voxel in the next chunk drops it") {
        const auto neighbour = [](const int x, const int y, const int z) -> VoxelID {
            return (x == CHUNK_SIZE && y == 1 && z == 1) ? 1 : 0;
        };
        REQUIRE(vertex_count(mesh(chunk, neighbour)) == 20);   // five faces, not six
    }
}

TEST_CASE("a voxel diagonally above darkens the corners under it", "[voxelmesh]") {
    // The top face of the lower voxel, with a voxel sitting diagonally over one
    // of its edges: the two corners on that side are occluded, the two on the
    // far side are not.
    VoxelChunk chunk{};
    set_voxel(chunk, 1, 1, 1, 1);
    set_voxel(chunk, 2, 1, 2, 1);

    const ChunkMeshData data = mesh(chunk);

    // The top face of the lower voxel: pointing up, at the height of its top.
    // Found by what it is rather than by where it sits in the vertex list, so
    // the test does not care what order the faces come out in.
    int open = 0;
    int occluded = 0;
    for (size_t v = 0; v < data.vertices.size() / 3; ++v) {
        const float ny = data.normals[v * 3 + 1];
        const float y = data.vertices[v * 3 + 1];
        if (ny < 0.5f || y != Catch::Approx(2.0f).margin(test::EPS)) continue;

        // The shade is in the alpha
        if (data.colors[v * 4 + 3] == 255) {
            open++;
        } else {
            occluded++;
            // The dark ones are the corners on the side the voxel above is on
            REQUIRE(data.vertices[v * 3] == Catch::Approx(2.0f).margin(test::EPS));
        }
    }

    REQUIRE(occluded == 2);
    REQUIRE(open == 2);
}

TEST_CASE("an open slab merges into one quad a side", "[voxelmesh][greedy]") {
    // A whole layer of one colour with nothing around it: every face of it is
    // evenly lit, so each of the six sides is one quad
    VoxelChunk chunk{};
    for (int y = 0; y < CHUNK_SIZE; ++y)
        for (int x = 0; x < CHUNK_SIZE; ++x) set_voxel(chunk, x, y, 3, 1);

    const ChunkMeshData data = mesh(chunk);
    REQUIRE(vertex_count(data) == 6 * 4);
    REQUIRE(area_facing(data, Vector3{0, 1, 0}) == Catch::Approx(CHUNK_SIZE * CHUNK_SIZE));
    REQUIRE(area_facing(data, Vector3{1, 0, 0}) == Catch::Approx(CHUNK_SIZE));
}

TEST_CASE("faces of different colours are not merged", "[voxelmesh][greedy]") {
    // The same slab, its two halves different colours: the top and the bottom
    // are two quads each, and the four sides are one or two
    VoxelChunk chunk{};
    for (int y = 0; y < CHUNK_SIZE; ++y)
        for (int x = 0; x < CHUNK_SIZE; ++x) set_voxel(chunk, x, y, 0, x < CHUNK_SIZE / 2 ? 1 : 2);

    const ChunkMeshData data = mesh(chunk);
    // Top 2, bottom 2, +X 1, -X 1, +Y 2, -Y 2
    REQUIRE(vertex_count(data) == 10 * 4);

    // Every vertex carries one of the two colours, never a blend
    const Color a = palette().at(1);
    const Color b = palette().at(2);
    for (size_t i = 0; i < data.colors.size(); i += 4) {
        const bool is_a = data.colors[i] == a.r && data.colors[i + 1] == a.g && data.colors[i + 2] == a.b;
        const bool is_b = data.colors[i] == b.r && data.colors[i + 1] == b.g && data.colors[i + 2] == b.b;
        REQUIRE((is_a || is_b));
    }
}

TEST_CASE("a shaded patch is left as single faces, the rest merged round it", "[voxelmesh][greedy]") {
    // A slab with one voxel standing on it: the faces round its foot have a
    // corner in its shadow and stay one quad each, everything else merges,
    // and the area is the same as before merging
    VoxelChunk chunk{};
    for (int y = 0; y < CHUNK_SIZE; ++y)
        for (int x = 0; x < CHUNK_SIZE; ++x) set_voxel(chunk, x, y, 0, 1);
    set_voxel(chunk, 8, 8, 1, 1);

    const ChunkMeshData data = mesh(chunk);
    // Up: the slab less the one covered face, plus the top of the voxel
    REQUIRE(area_facing(data, Vector3{0, 1, 0}) == Catch::Approx(CHUNK_SIZE * CHUNK_SIZE - 1 + 1));

    // The eight faces round the foot each come out on their own, with the
    // dark corners on the voxel's side
    int shaded = 0;
    for (size_t v = 0; v < data.vertices.size() / 3; ++v) {
        if (data.normals[v * 3 + 1] < 0.5f) continue;
        if (data.colors[v * 4 + 3] != 255) shaded++;
    }
    REQUIRE(shaded > 0);
    // Far fewer quads than one per face: the slab's top alone is 255 faces
    REQUIRE(vertex_count(data) < 40 * 4);
}

TEST_CASE("merging covers exactly the faces one quad per face would", "[voxelmesh][greedy]") {
    // A jumble of voxels in three colours: in every direction, the area of the
    // quads facing that way is the number of faces showing that way
    sim::Rng rng(42);
    VoxelChunk chunk{};
    for (int z = 0; z < CHUNK_SIZE; ++z)
        for (int y = 0; y < CHUNK_SIZE; ++y)
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                // Denser low down, like ground
                if (static_cast<int>(rng.next_below(CHUNK_SIZE)) >= z)
                    set_voxel(chunk, x, y, z, static_cast<VoxelID>(1 + rng.next_below(3)));
            }

    const ChunkMeshData data = mesh(chunk);
    for (int d = 0; d < 6; ++d) {
        REQUIRE(area_facing(data, world_normal(d)) == Catch::Approx(exposed_faces(chunk, d)));
    }
    // Every index points at a vertex
    for (const unsigned short i : data.indices) REQUIRE(i < vertex_count(data));
}

TEST_CASE("an all solid chunk with solid all round has no faces", "[voxelmesh]") {
    VoxelChunk chunk{};
    chunk.fill(1);
    const auto solid = [](int, int, int) -> VoxelID { return 1; };
    REQUIRE(mesh(chunk, solid).empty());
}
