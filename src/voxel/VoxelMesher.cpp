//
// Created by Andrei Ghita on 08.09.2025.
//

#include "voxel/VoxelMesher.hpp"
#include <raylib.h>
#include <array>
#include <vector>
#include <cstring> // memcpy
#include <raymath.h>
#include <rlgl.h>
#include "VoxelMap.hpp"
#include "game/main.hpp"

// How bright a face corner is at each of the four AO levels, 0 being a corner
// closed on both sides and 3 being open. Baked into the vertex colours, so
// changing these means remeshing.
static constexpr float AO_SHADE[4] = {0.4f, 0.6f, 0.8f, 1.0f};

// The chunk plus one voxel of its neighbours on every side, which is as far as
// a face or an AO corner ever reads.
static constexpr int PADDED_SIZE = CHUNK_SIZE + 2;

// Helper: linear index for (x,y,z_map) in chunk
static int idx(int x, int y, int z) {
    return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
}

// Helper: is inside current chunk
static bool inChunk(int x, int y, int z) {
    return (0 <= x && x < CHUNK_SIZE) &&
           (0 <= y && y < CHUNK_SIZE) &&
           (0 <= z && z < CHUNK_SIZE);
}

// Helper: linear index into the padded copy, where -1 is a legal coordinate
static int paddedIdx(int x, int y, int z) {
    return (x + 1) + (y + 1) * PADDED_SIZE + (z + 1) * PADDED_SIZE * PADDED_SIZE;
}

// Helper: one component of a corner offset, by axis number
static float axisValue(const Vector3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

int vertex_ao(const bool side1, const bool side2, const bool corner) {
    // Two solid sides meet in front of the corner, so whatever is diagonally
    // behind them cannot be seen and the corner is as dark as it gets
    if (side1 && side2) return 0;
    return 3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
}

Color palette_colour(const std::map<VoxelID, Color>& palette, const VoxelID id) {
    const auto it = palette.find(id);
    return it != palette.end() ? it->second : PURPLE;
}

ChunkMeshData build_chunk_mesh_data(const VoxelChunk& chunk, const VoxelNeighbourSampler& neighbour,
                                    const std::map<VoxelID, Color>& palette,
                                    const Vector3 origin, const float voxelSize) {
    // Neighbor directions in MAP space (x,y,z), and their normals in WORLD space
    struct Dir { int dx, dy, dz; Vector3 nWorld; };
    const Dir dirs[6] = {
        { +1,  0,  0, { +1,  0,  0 } }, // +X
        { -1,  0,  0, { -1,  0,  0 } }, // -X
        {  0, +1,  0, {  0,  0, +1 } }, // +Y map -> +Z world
        {  0, -1,  0, {  0,  0, -1 } }, // -Y map -> -Z world
        {  0,  0, +1, {  0, +1,  0 } }, // +Z map (up) -> +Y world
        {  0,  0, -1, {  0, -1,  0 } }, // -Z map (down) -> -Y world
    };

    // Four CCW corners per face in MAP space (relative to voxel min corner)
    constexpr std::array faceCornersMap = {
        // +X
        std::array{ Vector3{1,0,0}, Vector3{1,0,1}, Vector3{1,1,1}, Vector3{1,1,0} },
        // -X
        std::array{ Vector3{0,0,0}, Vector3{0,1,0}, Vector3{0,1,1}, Vector3{0,0,1} },
        // +Y (map)
        std::array{ Vector3{0,1,0}, Vector3{1,1,0}, Vector3{1,1,1}, Vector3{0,1,1} },
        // -Y (map)
        std::array{ Vector3{0,0,0}, Vector3{0,0,1}, Vector3{1,0,1}, Vector3{1,0,0} },
        // +Z (up)
        std::array{ Vector3{0,0,1}, Vector3{0,1,1}, Vector3{1,1,1}, Vector3{1,0,1} },
        // -Z (down)
        std::array{ Vector3{0,0,0}, Vector3{1,0,0}, Vector3{1,1,0}, Vector3{0,1,0} }
    };

    const float faceUV[8] = { 0,0,  1,0,  1,1,  0,1 };

    // The chunk with a one voxel shell of its neighbours around it. Everything
    // below reads this rather than the chunk, so a coordinate just outside is
    // an ordinary lookup: it is what lets a face be left out where the next
    // chunk is solid, and what an AO corner reads diagonally.
    std::vector<VoxelID> padded(PADDED_SIZE * PADDED_SIZE * PADDED_SIZE, 0);
    for (int z = -1; z <= CHUNK_SIZE; ++z) {
        for (int y = -1; y <= CHUNK_SIZE; ++y) {
            for (int x = -1; x <= CHUNK_SIZE; ++x) {
                VoxelID v = 0;
                if (inChunk(x, y, z)) v = chunk[idx(x, y, z)];
                else if (neighbour) v = neighbour(x, y, z);
                padded[paddedIdx(x, y, z)] = v;
            }
        }
    }
    auto solid_at = [&padded](const int x, const int y, const int z) {
        return padded[paddedIdx(x, y, z)] != 0;
    };

    // Each id's colour, looked up once per mesh rather than once per face
    std::array<Color, 256> colours{};
    std::array<bool, 256> coloured{};

    ChunkMeshData A;
    {
        // Room for a typical surface chunk's worth of quads up front
        constexpr size_t QUADS = 256;
        A.vertices.reserve(QUADS * 12);
        A.normals.reserve(QUADS * 12);
        A.uvs.reserve(QUADS * 8);
        A.colors.reserve(QUADS * 16);
        A.indices.reserve(QUADS * 6);
    }

    // The AO of each corner of face f of the voxel at (x, y, z), in corner order
    const auto face_ao = [&](const int x, const int y, const int z, const int f, int ao[4]) {
        const int faceAxis = f / 2;
        const int axisU = (faceAxis + 1) % 3;
        const int axisV = (faceAxis + 2) % 3;

        // The air voxel in front of the face, which is where an AO corner
        // looks around itself
        const int front[3] = {x + dirs[f].dx, y + dirs[f].dy, z + dirs[f].dz};

        for (int i = 0; i < 4; ++i) {
            const Vector3 cm = faceCornersMap[f][i];
            // Which way this corner sits in the face's own plane. The corner
            // offsets are 0 or 1 on each axis, so a 1 is the far side.
            const int stepU = axisValue(cm, axisU) > 0.5f ? 1 : -1;
            const int stepV = axisValue(cm, axisV) > 0.5f ? 1 : -1;

            int side1[3] = {front[0], front[1], front[2]};
            int side2[3] = {front[0], front[1], front[2]};
            int corner[3] = {front[0], front[1], front[2]};
            side1[axisU] += stepU;
            side2[axisV] += stepV;
            corner[axisU] += stepU;
            corner[axisV] += stepV;

            ao[i] = vertex_ao(
                solid_at(side1[0], side1[1], side1[2]),
                solid_at(side2[0], side2[1], side2[2]),
                solid_at(corner[0], corner[1], corner[2]));
        }
    };

    // A quad for face f of the voxel at (x, y, z), stretched to `w` voxels
    // along the face's U axis and `h` along its V axis (1 by 1 is one face),
    // coloured `colour` with the corner shades in `ao`
    const auto emit_quad = [&](const int x, const int y, const int z, const int f, const int w, const int h,
                               const Color colour, const int ao[4]) {
        const size_t baseIndex = A.vertices.size() / 3;
        const int faceAxis = f / 2;
        const int axisU = (faceAxis + 1) % 3;
        const int axisV = (faceAxis + 2) % 3;

        for (int i = 0; i < 4; ++i) {
            const Vector3 cm = faceCornersMap[f][i];
            // The far corners along U and V are moved out to the end of the
            // merged run; the axis the face looks along keeps its 0 or 1
            float m[3] = {static_cast<float>(x) + cm.x, static_cast<float>(y) + cm.y, static_cast<float>(z) + cm.z};
            if (axisValue(cm, axisU) > 0.5f) m[axisU] += static_cast<float>(w - 1);
            if (axisValue(cm, axisV) > 0.5f) m[axisV] += static_cast<float>(h - 1);

            // Map (x,y,z_map) -> World (X=x, Y=z_map, Z=y)
            A.vertices.push_back(origin.x + m[0] * voxelSize);
            A.vertices.push_back(origin.y + m[2] * voxelSize); // up
            A.vertices.push_back(origin.z + m[1] * voxelSize);

            A.normals.push_back(dirs[f].nWorld.x);
            A.normals.push_back(dirs[f].nWorld.y);
            A.normals.push_back(dirs[f].nWorld.z);

            A.uvs.push_back(faceUV[i*2 + 0]);
            A.uvs.push_back(faceUV[i*2 + 1]);

            A.colors.push_back(colour.r);
            A.colors.push_back(colour.g);
            A.colors.push_back(colour.b);
            A.colors.push_back(static_cast<unsigned char>(AO_SHADE[ao[i]] * 255.0f));
        }

        // Which way the quad is split matters once its corners differ: the
        // shade is interpolated across each triangle, so the wrong diagonal
        // leaves a crease running the other way. Splitting along the darker
        // diagonal is the usual rule (0fps.net's flipped quad).
        const auto at = [baseIndex](const int k) { return static_cast<unsigned short>(baseIndex + k); };
        if (ao[0] + ao[2] > ao[1] + ao[3]) {
            for (const int k : {1, 2, 3, 1, 3, 0}) A.indices.push_back(at(k));
        } else {
            for (const int k : {0, 1, 2, 0, 2, 3}) A.indices.push_back(at(k));
        }
    };

    // Every face, a direction and a slice at a time. A slice's faces go into a
    // mask, a 16 by 16 grid across the face's U and V axes, keyed by colour
    // and shade; a face whose four corners are not all one shade is emitted
    // straight away and left out of the mask, as it cannot be merged without
    // changing how it is shaded. The rest are merged greedily: a run along U,
    // grown along V for as long as every face in the next row matches.
    //
    // A chunk is at most 16^3 voxels, half of them showing six unmerged faces
    // in the worst case (a checkerboard), which is 49152 vertices: inside the
    // 65536 an unsigned short index reaches.
    constexpr uint32_t NO_FACE = 0;
    std::array<uint32_t, CHUNK_SIZE * CHUNK_SIZE> mask{};
    for (int f = 0; f < 6; ++f) {
        const int faceAxis = f / 2;
        const int axisU = (faceAxis + 1) % 3;
        const int axisV = (faceAxis + 2) % 3;

        for (int s = 0; s < CHUNK_SIZE; ++s) {
            mask.fill(NO_FACE);
            for (int j = 0; j < CHUNK_SIZE; ++j) {
                for (int i = 0; i < CHUNK_SIZE; ++i) {
                    int p[3];
                    p[faceAxis] = s;
                    p[axisU] = i;
                    p[axisV] = j;

                    const VoxelID v = chunk[idx(p[0], p[1], p[2])];
                    if (v == 0) continue; // air
                    if (solid_at(p[0] + dirs[f].dx, p[1] + dirs[f].dy, p[2] + dirs[f].dz)) continue;

                    if (!coloured[v]) {
                        colours[v] = palette_colour(palette, v);
                        coloured[v] = true;
                    }

                    int ao[4];
                    face_ao(p[0], p[1], p[2], f, ao);
                    if (ao[0] != ao[1] || ao[0] != ao[2] || ao[0] != ao[3]) {
                        emit_quad(p[0], p[1], p[2], f, 1, 1, colours[v], ao);
                        continue;
                    }
                    // id in the low byte, the shade above it, and a bit so a
                    // face is never the same as no face
                    mask[i + j * CHUNK_SIZE] = 0x10000u | (static_cast<uint32_t>(ao[0]) << 8) | v;
                }
            }

            for (int j = 0; j < CHUNK_SIZE; ++j) {
                for (int i = 0; i < CHUNK_SIZE;) {
                    const uint32_t key = mask[i + j * CHUNK_SIZE];
                    if (key == NO_FACE) { ++i; continue; }

                    int w = 1;
                    while (i + w < CHUNK_SIZE && mask[i + w + j * CHUNK_SIZE] == key) ++w;
                    int h = 1;
                    for (bool grow = true; grow && j + h < CHUNK_SIZE;) {
                        for (int k = 0; k < w; ++k) {
                            if (mask[i + k + (j + h) * CHUNK_SIZE] != key) { grow = false; break; }
                        }
                        if (grow) ++h;
                    }

                    int p[3];
                    p[faceAxis] = s;
                    p[axisU] = i;
                    p[axisV] = j;
                    const int shade = static_cast<int>((key >> 8) & 0xFFu);
                    const int ao[4] = {shade, shade, shade, shade};
                    emit_quad(p[0], p[1], p[2], f, w, h, colours[key & 0xFFu], ao);

                    for (int dj = 0; dj < h; ++dj) {
                        for (int k = 0; k < w; ++k) mask[i + k + (j + dj) * CHUNK_SIZE] = NO_FACE;
                    }
                    i += w;
                }
            }
        }
    }

    return A;
}

Mesh upload_chunk_mesh(const ChunkMeshData& A) {
    Mesh mesh = {0};
    if (A.empty()) return mesh;

    mesh.vertexCount   = static_cast<int>(A.vertices.size() / 3);
    mesh.triangleCount = static_cast<int>(A.indices.size() / 3);

    const auto copy = [](const auto& v) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        auto* out = static_cast<T*>(MemAlloc(static_cast<unsigned int>(v.size() * sizeof(T))));
        std::memcpy(out, v.data(), v.size() * sizeof(T));
        return out;
    };
    mesh.vertices = copy(A.vertices);
    mesh.normals = copy(A.normals);
    mesh.texcoords = copy(A.uvs);
    // The colour and the ambient occlusion ride in the vertex colours, which
    // the lighting shader reads as a colour and a shade. See lighting.fs.
    mesh.colors = copy(A.colors);
    mesh.indices = copy(A.indices);

    UploadMesh(&mesh, false); // static by default

    // The GPU has its own copy now. Only the vertices and indices are read on
    // the CPU again (the box round a selected grid), so the rest goes, which
    // is over half of what a chunk keeps in RAM. UnloadMesh() skips a null
    // array.
    MemFree(mesh.normals);
    MemFree(mesh.texcoords);
    MemFree(mesh.colors);
    mesh.normals = nullptr;
    mesh.texcoords = nullptr;
    mesh.colors = nullptr;

    return mesh;
}

Mesh build_chunk_mesh(const VoxelChunk& chunk, const VoxelNeighbourSampler& neighbour,
                      const std::map<VoxelID, Color>& palette, const Vector3 origin, const float voxelSize) {
    return upload_chunk_mesh(build_chunk_mesh_data(chunk, neighbour, palette, origin, voxelSize));
}

Model build_chunk_model(const Mesh mesh) {
    Model model = {0};
    model.transform = MatrixIdentity();

    if (mesh.vertexCount == 0) return model; // empty chunk → empty model

    model.meshCount = 1;
    model.meshes = static_cast<Mesh*>(MemAlloc(sizeof(Mesh)));
    model.meshes[0] = mesh;  // ownership passes to the model

    // White: the colours are in the vertices, and the shader multiplies this in
    model.materialCount = 1;
    model.materials = static_cast<Material*>(MemAlloc(sizeof(Material)));
    model.materials[0] = LoadMaterialDefault();
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    model.materials[0].shader = global::voxel_shader;

    model.meshMaterial = static_cast<int*>(MemAlloc(sizeof(int)));
    model.meshMaterial[0] = 0;

    return model;
}

void unload_chunk_model(Model& model) {
    // An empty chunk never allocated anything
    if (model.meshCount == 0) return;

    // Detach the shared shader, so UnloadMaterial() leaves it alone
    for (int i = 0; i < model.materialCount; ++i)
        model.materials[i].shader.id = rlGetShaderIdDefault();

    UnloadModel(model);
    model = Model{};
}
