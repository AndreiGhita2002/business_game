//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_TESTHELPERS_HPP
#define BUSINESS_GAME_TESTHELPERS_HPP
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
// raylib.h has to come before raymath.h: raymath defines Vector2/3/4 and Matrix
// itself when raylib has not already, and the other order is a redefinition.
// Every test file includes this header first for that reason.
#include <raylib.h>
#include <raymath.h>

#include "voxel/SingleChunkGrid.hpp"
#include "voxel/VoxelGrid.hpp"
#include "voxel/VoxelMap.hpp"

/**
 * Shared scaffolding for the unit tests.
 *
 * None of the tests open a window, so nothing in here may either. Grids are
 * built with a null VoxelView, which is only dereferenced by update_models(),
 * the one call that would need an OpenGL context. Tests must not call it.
 */

namespace test {

// Floats that have been through a compose and an undo, or through a file, land
// close rather than exactly, so comparisons carry a margin.
inline constexpr float EPS = 1e-4f;

/**
 * raylib logs a warning on every refusal the tests deliberately provoke, which
 * would bury the actual failures. Silenced once, before main() runs.
 */
inline int silence_raylib_logs() {
    SetTraceLogLevel(LOG_NONE);
    return 0;
}
inline const int logs_silenced = silence_raylib_logs();

/** A small palette, enough for the ids the tests place. */
inline VoxelColourMap make_palette() {
    auto colours = std::make_shared<std::map<VoxelID, Color>>();
    colours->insert({0, BLANK});   // air
    colours->insert({1, RED});
    colours->insert({2, GREEN});
    colours->insert({3, BLUE});
    return colours;
}

/** A grid with a single solid voxel at `solid`, and air everywhere else. */
inline std::unique_ptr<SingleChunkGrid> make_chunk_grid(const VoxelColourMap& palette,
                                                        const Int3 solid = Int3{1, 1, 1},
                                                        const VoxelID id = 1) {
    auto grid = std::make_unique<SingleChunkGrid>(nullptr, palette);
    grid->set_voxel(solid, id);
    return grid;
}

/**
 * A directory under the system temp path that deletes itself, so a test that
 * writes files leaves nothing behind even when it fails.
 */
class TempDir {
public:
    TempDir() {
        static std::atomic<int> counter{0};
        path_ = std::filesystem::temp_directory_path()
              / ("business_game_tests_" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /** A path inside the directory, for a file the test is about to write. */
    std::string file(const std::string& name) const {
        return (path_ / name).string();
    }

private:
    std::filesystem::path path_;
};

}

// Component wise comparisons as macros rather than functions, so that a failure
// is reported at the line of the test that failed rather than inside a helper.

#define REQUIRE_VEC3_EQ(actual, expected)                                      \
    do {                                                                       \
        const Vector3 actual_ = (actual);                                      \
        const Vector3 expected_ = (expected);                                  \
        REQUIRE(actual_.x == Catch::Approx(expected_.x).margin(test::EPS));    \
        REQUIRE(actual_.y == Catch::Approx(expected_.y).margin(test::EPS));    \
        REQUIRE(actual_.z == Catch::Approx(expected_.z).margin(test::EPS));    \
    } while (false)

// A quaternion and its negation are the same rotation, so the sign is settled
// before the components are compared.
#define REQUIRE_QUAT_EQ(actual, expected)                                      \
    do {                                                                       \
        const Quaternion actual_ = (actual);                                   \
        Quaternion expected_ = (expected);                                     \
        const float dot_ = actual_.x * expected_.x + actual_.y * expected_.y   \
                         + actual_.z * expected_.z + actual_.w * expected_.w;  \
        if (dot_ < 0.0f) {                                                     \
            expected_ = Quaternion{-expected_.x, -expected_.y,                 \
                                   -expected_.z, -expected_.w};                \
        }                                                                      \
        REQUIRE(actual_.x == Catch::Approx(expected_.x).margin(test::EPS));    \
        REQUIRE(actual_.y == Catch::Approx(expected_.y).margin(test::EPS));    \
        REQUIRE(actual_.z == Catch::Approx(expected_.z).margin(test::EPS));    \
        REQUIRE(actual_.w == Catch::Approx(expected_.w).margin(test::EPS));    \
    } while (false)

#define REQUIRE_TRANSFORM_EQ(actual, expected)                                 \
    do {                                                                       \
        const Transform actual_t_ = (actual);                                  \
        const Transform expected_t_ = (expected);                              \
        REQUIRE_VEC3_EQ(actual_t_.translation, expected_t_.translation);       \
        REQUIRE_QUAT_EQ(actual_t_.rotation, expected_t_.rotation);             \
        REQUIRE_VEC3_EQ(actual_t_.scale, expected_t_.scale);                   \
    } while (false)

#endif //BUSINESS_GAME_TESTHELPERS_HPP
