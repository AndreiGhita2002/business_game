//
// Created by Andrei Ghita on 08.09.2025.
//

#include "voxel/VoxelMesher.hpp"
#include <raylib.h>
#include <array>
#include <unordered_map>
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

std::vector<MaterialMeshData>
build_chunk_mesh_data(const VoxelChunk& chunk, const VoxelNeighbourSampler& neighbour,
                      const Vector3 origin, const float voxelSize) {
    //TODO (optimisation)
    // the chunk mesher could be massively improved if it was switched to a greedy algorithm

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

    // Accumulate per material id
    std::unordered_map<VoxelID, MaterialMeshData> byMat;
    byMat.reserve(8);

    auto emitFace = [&](MaterialMeshData& A, int x, int y, int z, int f) {
        const float bx = static_cast<float>(x);
        const float by = static_cast<float>(y);
        const float bz = static_cast<float>(z);

        const size_t baseIndex = A.vertices.size() / 3;

        // The axis the face looks along, and the two that lie in its plane
        const int faceAxis = f / 2;
        const int axisU = (faceAxis + 1) % 3;
        const int axisV = (faceAxis + 2) % 3;

        // The air voxel in front of the face, which is where an AO corner
        // looks around itself
        const int front[3] = {x + dirs[f].dx, y + dirs[f].dy, z + dirs[f].dz};

        int ao[4] = {3, 3, 3, 3};

        for (int i = 0; i < 4; ++i) {
            const Vector3 cm = faceCornersMap[f][i];
            const float mx = bx + cm.x;
            const float my = by + cm.y;
            const float mz = bz + cm.z;

            // Map (x,y,z_map) -> World (X=x, Y=z_map, Z=y)
            const float wx = origin.x + mx * voxelSize;
            const float wy = origin.y + mz * voxelSize; // up
            const float wz = origin.z + my * voxelSize;

            A.vertices.push_back(wx);
            A.vertices.push_back(wy);
            A.vertices.push_back(wz);

            A.normals.push_back(dirs[f].nWorld.x);
            A.normals.push_back(dirs[f].nWorld.y);
            A.normals.push_back(dirs[f].nWorld.z);

            A.uvs.push_back(faceUV[i*2 + 0]);
            A.uvs.push_back(faceUV[i*2 + 1]);

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

            const auto shade = static_cast<unsigned char>(AO_SHADE[ao[i]] * 255.0f);
            A.colors.push_back(shade);
            A.colors.push_back(shade);
            A.colors.push_back(shade);
            A.colors.push_back(255);
        }

        // Which way the quad is split matters once its corners differ: the
        // shade is interpolated across each triangle, so the wrong diagonal
        // leaves a crease running the other way. Splitting along the darker
        // diagonal is the usual rule (0fps.net's flipped quad).
        if (ao[0] + ao[2] > ao[1] + ao[3]) {
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 1));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 2));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 3));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 1));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 3));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 0));
        } else {
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 0));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 1));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 2));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 0));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 2));
            A.indices.push_back(static_cast<unsigned short>(baseIndex + 3));
        }
    };

    // Walk voxels: add faces only when the neighbor is AIR (0), which now
    // includes the neighbour in the next chunk along
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                VoxelID v = chunk[idx(x,y,z)];
                if (v == 0) continue; // air

                // Looked up once for the voxel, and only once it has a face
                MaterialMeshData* A = nullptr;
                for (int f = 0; f < 6; ++f) {
                    const int nx = x + dirs[f].dx;
                    const int ny = y + dirs[f].dy;
                    const int nz = z + dirs[f].dz;

                    if (!solid_at(nx, ny, nz)) {
                        if (A == nullptr) {
                            A = &byMat[v];
                            if (A->vertices.empty()) {
                                // Room for a chunk's worth of faces up front,
                                // rather than growing a face at a time
                                constexpr size_t FACES = 256;
                                A->id = v;
                                A->vertices.reserve(FACES * 12);
                                A->normals.reserve(FACES * 12);
                                A->uvs.reserve(FACES * 8);
                                A->colors.reserve(FACES * 16);
                                A->indices.reserve(FACES * 6);
                            }
                        }
                        emitFace(*A, x, y, z, f);
                    }
                }
            }
        }
    }

    std::vector<MaterialMeshData> result;
    result.reserve(byMat.size());
    for (auto& [id, data] : byMat) result.push_back(std::move(data));
    return result;
}

std::vector<MaterialMesh> upload_chunk_mesh(const std::vector<MaterialMeshData>& data) {
    std::vector<MaterialMesh> result;
    result.reserve(data.size());

    for (const MaterialMeshData& A : data) {
        Mesh mesh = {0};
        mesh.vertexCount   = static_cast<int>(A.vertices.size() / 3);
        mesh.triangleCount = static_cast<int>(A.indices.size() / 3);

        if (!A.vertices.empty()) {
            mesh.vertices = (float*)MemAlloc(A.vertices.size() * sizeof(float));
            std::memcpy(mesh.vertices, A.vertices.data(), A.vertices.size() * sizeof(float));
        }
        if (!A.normals.empty()) {
            mesh.normals = (float*)MemAlloc(A.normals.size() * sizeof(float));
            std::memcpy(mesh.normals, A.normals.data(), A.normals.size() * sizeof(float));
        }
        if (!A.uvs.empty()) {
            mesh.texcoords = (float*)MemAlloc(A.uvs.size() * sizeof(float));
            std::memcpy(mesh.texcoords, A.uvs.data(), A.uvs.size() * sizeof(float));
        }
        // The ambient occlusion rides in the vertex colours, which the lighting
        // shader reads as a shade rather than as a tint. See lighting.fs.
        if (!A.colors.empty()) {
            mesh.colors = (unsigned char*)MemAlloc(A.colors.size() * sizeof(unsigned char));
            std::memcpy(mesh.colors, A.colors.data(), A.colors.size() * sizeof(unsigned char));
        }
        if (!A.indices.empty()) {
            mesh.indices = (unsigned short*)MemAlloc(A.indices.size() * sizeof(unsigned short));
            std::memcpy(mesh.indices, A.indices.data(), A.indices.size() * sizeof(unsigned short));
        }

        UploadMesh(&mesh, false); // static by default

        // The GPU has its own copy now. Only the vertices and indices are
        // read on the CPU again (picking, and the box round a selected grid),
        // so the rest goes, which is over half of what a chunk keeps in RAM.
        // UnloadMesh() skips a null array.
        MemFree(mesh.normals);
        MemFree(mesh.texcoords);
        MemFree(mesh.colors);
        mesh.normals = nullptr;
        mesh.texcoords = nullptr;
        mesh.colors = nullptr;

        result.push_back(MaterialMesh{ A.id, mesh });
    }

    return result;
}

std::vector<MaterialMesh>
build_chunk_mesh(const VoxelChunk& chunk, const VoxelNeighbourSampler& neighbour,
                 const Vector3 origin, const float voxelSize) {
    return upload_chunk_mesh(build_chunk_mesh_data(chunk, neighbour, origin, voxelSize));
}

Model build_chunk_model(const std::vector<MaterialMesh> &mats, const std::map<VoxelID, Color> &voxelColourMap) {
    Model model = {0};
    model.transform = MatrixIdentity();

    const int n = (int)mats.size();
    if (n == 0) return model; // empty chunk → empty model

    // 1) Attach meshes (each entry already has GPU buffers)
    model.meshCount = n;
    model.meshes = (Mesh*)MemAlloc(sizeof(Mesh) * n);
    for (int i = 0; i < n; ++i) {
        model.meshes[i] = mats[i].mesh;  // transfer ownership to the model
    }

    // 2) Create materials (one per mesh, colored by VoxelID)
    model.materialCount = n;
    model.materials = (Material*)MemAlloc(sizeof(Material) * n);
    for (int i = 0; i < n; ++i) {
        model.materials[i] = LoadMaterialDefault();
        Color c = PURPLE;
        if (auto it = voxelColourMap.find(mats[i].id); it != voxelColourMap.end())
            c = it->second;

        model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = c;
        model.materials[i].shader = global::voxel_shader;
    }

    // 3) Map each mesh to its material
    model.meshMaterial = (int*)MemAlloc(sizeof(int) * n);
    for (int i = 0; i < n; ++i) model.meshMaterial[i] = i;

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
