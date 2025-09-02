//
// Created by Andrei Ghita on 01.09.2025.
//

#ifndef BUSINESS_GAME_GAMEMAP_HPP
#define BUSINESS_GAME_GAMEMAP_HPP
#include <Color.hpp>
#include <cstdint>
#include <map>

// REMINDER: Z goes UP/DOWN

class GameMap {

public:
    #define CHUNK_SIZE 16
    using VoxelID = uint8_t;
    using VoxelChunk = std::array<VoxelID,CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE>;
    using Int2 = std::pair<int,int>;
    using Int3 = std::tuple<int,int,int>;

    uint32_t size_x, size_y;
    uint32_t chunks_x, chunks_y;
    std::map<VoxelID, Color> voxelColourMap;
    std::map<Int2, VoxelChunk> chunkMap;

    GameMap(uint32_t size_x, uint32_t size_y);
    ~GameMap();

    VoxelChunk* get_chunk(Int2 pos);
    VoxelID* get_voxel(Int3 pos);
    static VoxelID* get_chunk_voxel(VoxelChunk& chunk, Int3 pos);

private:
    // helper: floor division/modulo that work for negatives
    static int floordiv(int a, int b) {
        int q = a / b;
        int r = a % b;
        return (r && ((r > 0) != (b > 0))) ? (q - 1) : q;
    }
    static int floormod(int a, int b) {
        int m = a % b;
        return (m < 0) ? (m + (b > 0 ? b : -b)) : m;
    }
};


#endif //BUSINESS_GAME_GAMEMAP_HPP