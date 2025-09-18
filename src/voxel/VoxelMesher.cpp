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
#include "VoxelMap.hpp"
#include "game/main.hpp"


// Helper: linear index for (x,y,z_map) in chunk
inline int idx(int x, int y, int z) {
    return x + y * CHUNK_SIZE + z * CHUNK_SIZE * CHUNK_SIZE;
}

// Helper: is inside current chunk
inline bool inChunk(int x, int y, int z) {
    return (0 <= x && x < CHUNK_SIZE) &&
           (0 <= y && y < CHUNK_SIZE) &&
           (0 <= z && z < CHUNK_SIZE);
}

struct Accum {
    std::vector<float> vertices;   // 3 per vertex
    std::vector<float> normals;    // 3 per vertex
    std::vector<float> uvs;        // 2 per vertex (keep simple 0..1)
    std::vector<unsigned short> indices; // 3 per triangle
};

std::vector<MaterialMesh>
build_chunk_mesh(const VoxelChunk& chunk, Vector3 origin, float voxelSize) {
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
    const std::array<std::array<Vector3,4>, 6> faceCornersMap = {
        // +X
        std::array<Vector3,4>{ Vector3{1,0,0}, Vector3{1,0,1}, Vector3{1,1,1}, Vector3{1,1,0} },
        // -X
        std::array<Vector3,4>{ Vector3{0,0,0}, Vector3{0,1,0}, Vector3{0,1,1}, Vector3{0,0,1} },
        // +Y (map)
        std::array<Vector3,4>{ Vector3{0,1,0}, Vector3{1,1,0}, Vector3{1,1,1}, Vector3{0,1,1} },
        // -Y (map)
        std::array<Vector3,4>{ Vector3{0,0,0}, Vector3{0,0,1}, Vector3{1,0,1}, Vector3{1,0,0} },
        // +Z (up)
        std::array<Vector3,4>{ Vector3{0,0,1}, Vector3{0,1,1}, Vector3{1,1,1}, Vector3{1,0,1} },
        // -Z (down)
        std::array<Vector3,4>{ Vector3{0,0,0}, Vector3{1,0,0}, Vector3{1,1,0}, Vector3{0,1,0} }
    };

    const float faceUV[8] = { 0,0,  1,0,  1,1,  0,1 };

    // Accumulate per material id
    std::unordered_map<VoxelID, Accum> byMat;
    byMat.reserve(8);

    auto emitFace = [&](Accum& A, int x, int y, int z, int f) {
        const float bx = static_cast<float>(x);
        const float by = static_cast<float>(y);
        const float bz = static_cast<float>(z);

        const size_t baseIndex = A.vertices.size() / 3;

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
        }

        // Two triangles (0,1,2) and (0,2,3)
        A.indices.push_back(static_cast<unsigned short>(baseIndex + 0));
        A.indices.push_back(static_cast<unsigned short>(baseIndex + 1));
        A.indices.push_back(static_cast<unsigned short>(baseIndex + 2));
        A.indices.push_back(static_cast<unsigned short>(baseIndex + 0));
        A.indices.push_back(static_cast<unsigned short>(baseIndex + 2));
        A.indices.push_back(static_cast<unsigned short>(baseIndex + 3));
    };

    // Walk voxels: add faces only when neighbor is AIR (0)
    for (int z = 0; z < CHUNK_SIZE; ++z) {
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                VoxelID v = chunk[idx(x,y,z)];
                if (v == 0) continue; // air

                for (int f = 0; f < 6; ++f) {
                    const int nx = x + dirs[f].dx;
                    const int ny = y + dirs[f].dy;
                    const int nz = z + dirs[f].dz;

                    bool neighborSolid = false;
                    if (inChunk(nx, ny, nz)) {
                        neighborSolid = (chunk[idx(nx,ny,nz)] != 0);
                    } else {
                        // Treat OOB as air; stitch with neighbor chunks later if desired
                        neighborSolid = false;
                    }
                    if (!neighborSolid) {
                        Accum& A = byMat[v];
                        emitFace(A, x, y, z, f);
                    }
                }
            }
        }
    }

    // Convert accumulators to GPU meshes
    std::vector<MaterialMesh> result;
    result.reserve(byMat.size());
    for (auto& [id, A] : byMat) {
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
        if (!A.indices.empty()) {
            mesh.indices = (unsigned short*)MemAlloc(A.indices.size() * sizeof(unsigned short));
            std::memcpy(mesh.indices, A.indices.data(), A.indices.size() * sizeof(unsigned short));
        }

        UploadMesh(&mesh, false); // static by default
        result.push_back(MaterialMesh{ id, mesh });
    }

    return result;
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
