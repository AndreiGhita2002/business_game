//
// Created by Andrei Ghita on 16.09.2025.
//

#ifndef BUSINESS_GAME_VOXELGRID_HPP
#define BUSINESS_GAME_VOXELGRID_HPP
#include <raylib.h>
#include <map>

// REMINDER: Z goes UP/DOWN

#define CHUNK_SIZE 16
using VoxelID = uint8_t;
using VoxelChunk = std::array<VoxelID,CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE>;
using VoxelColourMap = std::shared_ptr<std::map<VoxelID, Color>>;

struct Int2 {
    int x;
    int y;

    bool operator<(const Int2& other) const noexcept {
        if (x < other.x) return true;
        if (x > other.x) return false;
        return y < other.y;
    }

    bool operator==(const Int2& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

struct Int3 {
    int x;
    int y;
    int z;

    bool operator<(const Int3& other) const noexcept {
        if (x < other.x) return true;
        if (x > other.x) return false;
        if (y < other.y) return true;
        if (y > other.y) return false;
        return z < other.z;
    }

    bool operator==(const Int3& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct ModelInfo {
    bool do_render;
    Model model;
    Transform transform;
};

class VoxelGrid {
public:
    Transform transform;
    VoxelColourMap voxel_colours;

    virtual Int2 get_size() = 0;
    virtual VoxelID* get_voxel(Int3 grid_pos) = 0;
    virtual void update_models() = 0;
    //TODO (optimisation)
    // this function could be improved by having it not create
    // a new vector every time. Ideally, there should be a linked
    // list where every node has an array of ModelInfos that are
    // managed by their respective grid
    virtual std::vector<ModelInfo*> get_models() = 0;

    virtual ~VoxelGrid() = default;

protected:
    // helper: floor division/modulo that work for negatives
    static int floordiv(const int a, const int b) {
        int q = a / b;
        int r = a % b;
        return (r && ((r > 0) != (b > 0))) ? (q - 1) : q;
    }
    static int floormod(const int a, const int b) {
        int m = a % b;
        return (m < 0) ? (m + (b > 0 ? b : -b)) : m;
    }
};


inline Transform identity() {
    return Transform{
        Vector3 {0.0, 0.0, 0.0},
        Quaternion {0.0, 0.0, 0.0, 1.0},
        Vector3 {1.0, 1.0, 1.0},
    };
}

#endif //BUSINESS_GAME_VOXELGRID_HPP