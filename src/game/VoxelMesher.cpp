//
// Created by Andrei Ghita on 08.09.2025.
//

#include <raylib.h>
#include <vector>
#include <array>
#include <cstdint>
#include <cstring> // memcpy
#include "VoxelMap.hpp"


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

// Build a Mesh from a single chunk.
// - origin: world-space translation of the chunk corner at (0,0,0) in map space
// - voxelSize: world units for one voxel edge (default 1.0)
Mesh build_chunk_mesh(const VoxelChunk& chunk, Vector3 origin, float voxelSize) {
    std::vector<float> vertices;   // 3 floats per vertex
    std::vector<float> normals;    // 3 floats per vertex
    std::vector<float> uvs;        // 2 floats per vertex
    std::vector<unsigned short> indices; // 3 per triangle

    vertices.reserve(10000);
    normals.reserve(10000);
    uvs.reserve(8000);
    indices.reserve(15000);

    // Neighbor directions in MAP space (x,y,z_map)
    struct Dir { int dx, dy, dz; Vector3 nWorld; }; // nWorld is normal in DRAW space
    // Map-space axis: z is up. Draw-space axis: Y is up, Z is map.y.
    const Dir dirs[6] = {
        { +1,  0,  0, { +1,  0,  0 } }, // +X face -> +X world
        { -1,  0,  0, { -1,  0,  0 } }, // -X face
        {  0, +1,  0, {  0,  0, +1 } }, // +Y map -> +Z world
        {  0, -1,  0, {  0,  0, -1 } }, // -Y map -> -Z world
        {  0,  0, +1, {  0, +1,  0 } }, // +Z map (up) -> +Y world
        {  0,  0, -1, {  0, -1,  0 } }, // -Z map (down) -> -Y world
    };

    // For each face, the 4 corner offsets in MAP space (counter-clockwise when looking from outside)
    // Each entry: 4 corners (x,y,z_map) relative to voxel min corner.
    const std::array<std::array<Vector3,4>, 6> faceCornersMap = {
        // +X
        std::array<Vector3,4>{ Vector3{1,0,0}, Vector3{1,0,1}, Vector3{1,1,1}, Vector3{1,1,0} },
        // -X
        std::array<Vector3,4>{ Vector3{0,0,0}, Vector3{0,1,0}, Vector3{0,1,1}, Vector3{0,0,1} },
        // +Y (map) -> +Z (world)
        std::array<Vector3,4>{ Vector3{0,1,0}, Vector3{1,1,0}, Vector3{1,1,1}, Vector3{0,1,1} },
        // -Y (map)
        std::array<Vector3,4>{ Vector3{0,0,0}, Vector3{0,0,1}, Vector3{1,0,1}, Vector3{1,0,0} },
        // +Z (map up) -> +Y (world)
        std::array<Vector3,4>{ Vector3{0,0,1}, Vector3{0,1,1}, Vector3{1,1,1}, Vector3{1,0,1} },
        // -Z (map down)
        std::array<Vector3,4>{ Vector3{0,0,0}, Vector3{1,0,0}, Vector3{1,1,0}, Vector3{0,1,0} }
    };

    // Shared quad UVs (simple 0..1)
    const float faceUV[8] = { 0,0,  1,0,  1,1,  0,1 };

    auto emitFace = [&](int x, int y, int z, int f) {
        // Base position of this voxel in MAP space
        const float bx = static_cast<float>(x);
        const float by = static_cast<float>(y);
        const float bz = static_cast<float>(z);

        const size_t baseIndex = vertices.size() / 3;

        // 4 corners
        for (int i = 0; i < 4; ++i) {
            const Vector3 cm = faceCornersMap[f][i];
            // Map-space corner
            const float mx = bx + cm.x;
            const float my = by + cm.y;
            const float mz = bz + cm.z;

            // Convert to DRAW/world space: (X = mx, Y = mz, Z = my)
            const float wx = origin.x + mx * voxelSize;
            const float wy = origin.y + mz * voxelSize; // up
            const float wz = origin.z + my * voxelSize;

            vertices.push_back(wx);
            vertices.push_back(wy);
            vertices.push_back(wz);

            // Normal in world space
            normals.push_back(dirs[f].nWorld.x);
            normals.push_back(dirs[f].nWorld.y);
            normals.push_back(dirs[f].nWorld.z);

            // UVs
            uvs.push_back(faceUV[i*2 + 0]);
            uvs.push_back(faceUV[i*2 + 1]);
        }

        // Two triangles: (0,1,2) and (0,2,3) in the four new vertices
        indices.push_back(static_cast<unsigned short>(baseIndex + 0));
        indices.push_back(static_cast<unsigned short>(baseIndex + 1));
        indices.push_back(static_cast<unsigned short>(baseIndex + 2));
        indices.push_back(static_cast<unsigned short>(baseIndex + 0));
        indices.push_back(static_cast<unsigned short>(baseIndex + 2));
        indices.push_back(static_cast<unsigned short>(baseIndex + 3));
    };

    // Walk all voxels
    for (int z = 0; z < CHUNK_SIZE; ++z) {          // map z = up
        for (int y = 0; y < CHUNK_SIZE; ++y) {
            for (int x = 0; x < CHUNK_SIZE; ++x) {
                VoxelID v = chunk[idx(x,y,z)];
                if (v == 0) continue; // air

                // For each face, check neighbor; if empty or out-of-bounds, emit face
                for (int f = 0; f < 6; ++f) {
                    const int nx = x + dirs[f].dx;
                    const int ny = y + dirs[f].dy;
                    const int nz = z + dirs[f].dz;

                    bool neighborSolid = false;
                    if (inChunk(nx, ny, nz)) {
                        neighborSolid = (chunk[idx(nx,ny,nz)] != 0);
                    } else {
                        // Outside this chunk: treat as air for now (you can query neighboring chunks later)
                        neighborSolid = false;
                    }

                    if (!neighborSolid) emitFace(x, y, z, f);
                }
            }
        }
    }

    // Build the raylib Mesh
    Mesh mesh = {0};
    mesh.vertexCount   = static_cast<int>(vertices.size() / 3);
    mesh.triangleCount = static_cast<int>(indices.size() / 3);

    // Allocate and copy to CPU arrays expected by raylib
    if (!vertices.empty()) {
        mesh.vertices = (float*)MemAlloc(vertices.size() * sizeof(float));
        std::memcpy(mesh.vertices, vertices.data(), vertices.size() * sizeof(float));
    }
    if (!normals.empty()) {
        mesh.normals = (float*)MemAlloc(normals.size() * sizeof(float));
        std::memcpy(mesh.normals, normals.data(), normals.size() * sizeof(float));
    }
    if (!uvs.empty()) {
        mesh.texcoords = (float*)MemAlloc(uvs.size() * sizeof(float));
        std::memcpy(mesh.texcoords, uvs.data(), uvs.size() * sizeof(float));
    }
    if (!indices.empty()) {
        mesh.indices = (unsigned short*)MemAlloc(indices.size() * sizeof(unsigned short));
        std::memcpy(mesh.indices, indices.data(), indices.size() * sizeof(unsigned short));
    }

    // Upload to GPU (false = static)
    UploadMesh(&mesh, false);
    return mesh;
}
