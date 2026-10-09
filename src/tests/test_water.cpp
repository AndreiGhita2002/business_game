//
// Created by Claude on 09.10.2026.
//

#include "TestHelpers.hpp"

#include "game/Frustum.hpp"
#include "water/WaterView.hpp"

/**
 * The parts of the water that need no window: how the plane is cut into
 * chunks, where its surface is, and which chunks a camera keeps. Drawing it
 * needs the GPU, so the WaterView itself is never built here.
 */

using Catch::Approx;

// Half a side of the shared chunk mesh, which is centred on the origin: where
// its corners are before chunk_matrix() places it
constexpr float MESH_HALF = WATER_CHUNK_SIZE / 2.0f;

TEST_CASE("a whole number of chunks covers the area exactly", "[water]") {
    const auto chunks = water_chunk_layout(128, 64, 16);
    REQUIRE(chunks.size() == 8 * 4);

    float area = 0.0f;
    for (const WaterChunk& c : chunks) {
        REQUIRE(c.width == 16.0f);
        REQUIRE(c.depth == 16.0f);
        area += c.width * c.depth;
    }
    REQUIRE(area == 128.0f * 64.0f);

    // Row by row along x, from the origin
    REQUIRE(chunks[0].x == 0.0f);
    REQUIRE(chunks[0].z == 0.0f);
    REQUIRE(chunks[1].x == 16.0f);
    REQUIRE(chunks[1].z == 0.0f);
    REQUIRE(chunks[8].x == 0.0f);
    REQUIRE(chunks[8].z == 16.0f);
}

TEST_CASE("the last row and column are cut short to fit", "[water]") {
    const auto chunks = water_chunk_layout(20, 40, 16);
    REQUIRE(chunks.size() == 2 * 3);

    float area = 0.0f;
    for (const WaterChunk& c : chunks) {
        REQUIRE(c.x + c.width <= 20.0f);
        REQUIRE(c.z + c.depth <= 40.0f);
        area += c.width * c.depth;
    }
    REQUIRE(area == 20.0f * 40.0f);

    // The corner chunk is short on both sides
    REQUIRE(chunks.back().width == 4.0f);
    REQUIRE(chunks.back().depth == 8.0f);
}

TEST_CASE("nothing to cover gives no chunks", "[water]") {
    REQUIRE(water_chunk_layout(0, 64, 16).empty());
    REQUIRE(water_chunk_layout(64, 0, 16).empty());
    REQUIRE(water_chunk_layout(64, 64, 0).empty());
}

TEST_CASE("the surface sits just under the top of its layer", "[water]") {
    // Level 1 fills layers 0 and 1, which run up to world Y = 2
    REQUIRE(water_surface_height(1) == Approx(2.0f - WATER_SURFACE_INSET));
    REQUIRE(water_surface_height(1) > 1.0f);
    REQUIRE(water_surface_height(1) < 2.0f);
    REQUIRE(water_surface_height(0) == Approx(1.0f - WATER_SURFACE_INSET));
}

TEST_CASE("a chunk's box is the chunk, around the surface", "[water]") {
    const WaterChunk chunk{32.0f, 48.0f, 16.0f, 8.0f};
    const BoundingBox box = water_chunk_bounds(chunk, 1.5f);
    REQUIRE(box.min.x == 32.0f);
    REQUIRE(box.max.x == 48.0f);
    REQUIRE(box.min.z == 48.0f);
    REQUIRE(box.max.z == 56.0f);
    REQUIRE(box.min.y < 1.5f);
    REQUIRE(box.max.y > 1.5f);
}

TEST_CASE("a camera over one corner of the map keeps only the chunks near it", "[water]") {
    const auto chunks = water_chunk_layout(128, 128, WATER_CHUNK_SIZE);
    const float surface = water_surface_height(1);

    // Above the corner at the origin, looking straight down (up is +z so the
    // look-at basis is not degenerate), seeing 40 units across at most
    const Matrix view = MatrixLookAt(Vector3{8, 30, 8}, Vector3{8, 0, 8}, Vector3{0, 0, 1});
    const Matrix projection = MatrixPerspective(45.0 * DEG2RAD, 1.0, 0.1, 100.0);
    const Frustum f = frustum_from_matrix(MatrixMultiply(view, projection));

    size_t visible = 0;
    for (const WaterChunk& c : chunks) {
        if (frustum_contains_box(f, water_chunk_bounds(c, surface))) visible++;
    }
    REQUIRE(frustum_contains_box(f, water_chunk_bounds(chunks[0], surface)));
    REQUIRE_FALSE(frustum_contains_box(f, water_chunk_bounds(chunks.back(), surface)));
    REQUIRE(visible > 0);
    REQUIRE(visible < 8);
}

// The waves, through water_wave_height(), the C++ twin of the vertex shader's
// wave_height(). A 0.1 high, 8 long wave that takes 2 seconds to move along by
// a wavelength.
namespace {
constexpr float AMP = 0.1f;
constexpr float LEN = 8.0f;
constexpr float PERIOD = 2.0f;
float wave(const float x, const float z, const float t) {
    return water_wave_height(x, z, t, AMP, LEN, PERIOD);
}
}

TEST_CASE("the wave stays within its amplitude", "[water][waves]") {
    for (int i = 0; i < 200; ++i) {
        const float h = wave(static_cast<float>(i) * 0.37f, static_cast<float>(i) * -0.21f, i * 0.05f);
        REQUIRE(h <= AMP + test::EPS);
        REQUIRE(h >= -AMP - test::EPS);
    }
}

TEST_CASE("the wave peaks a quarter wavelength out at time 0", "[water][waves]") {
    REQUIRE(wave(0.0f, 0.0f, 0.0f) == Approx(0.0f).margin(test::EPS));
    REQUIRE(wave(LEN / 4.0f, 0.0f, 0.0f) == Approx(AMP));
    REQUIRE(wave(3.0f * LEN / 4.0f, 0.0f, 0.0f) == Approx(-AMP));
}

TEST_CASE("the wave depends only on the distance from the origin", "[water][waves]") {
    // Five units out along x, along z, and on a diagonal (3, 4)
    const float along_x = wave(5.0f, 0.0f, 0.7f);
    REQUIRE(wave(0.0f, 5.0f, 0.7f) == Approx(along_x));
    REQUIRE(wave(-5.0f, 0.0f, 0.7f) == Approx(along_x));
    REQUIRE(wave(3.0f, 4.0f, 0.7f) == Approx(along_x));
}

TEST_CASE("the wave repeats every period and every wavelength", "[water][waves]") {
    REQUIRE(wave(3.0f, 1.0f, 0.4f + PERIOD) == Approx(wave(3.0f, 1.0f, 0.4f)).margin(test::EPS));
    REQUIRE(wave(3.0f + LEN, 0.0f, 0.4f) == Approx(wave(3.0f, 0.0f, 0.4f)).margin(test::EPS));
}

TEST_CASE("the crests run outwards", "[water][waves]") {
    // A crest moves a wavelength per period, so half a second later the same
    // height is found a quarter of a wavelength further out
    const float now = wave(2.0f, 0.0f, 0.3f);
    REQUIRE(wave(2.0f + LEN / 4.0f, 0.0f, 0.3f + PERIOD / 4.0f) == Approx(now).margin(test::EPS));
}

TEST_CASE("a wave with no length or period is still water", "[water][waves]") {
    REQUIRE(water_wave_height(2.0f, 0.0f, 0.5f, AMP, 0.0f, PERIOD) == 0.0f);
    REQUIRE(water_wave_height(2.0f, 0.0f, 0.5f, AMP, LEN, 0.0f) == 0.0f);
}

TEST_CASE("a chunk sits where it is laid out when the terrain is at the origin", "[water]") {
    const WaterChunk chunk{32.0f, 16.0f, 16.0f, 16.0f};
    const Matrix m = WaterView::chunk_matrix(chunk, 1.875f, MatrixIdentity());

    // The shared mesh is centred on the origin, so its centre lands on the
    // chunk's centre and its corner on the chunk's corner
    REQUIRE_VEC3_EQ(Vector3Transform(Vector3{0, 0, 0}, m), (Vector3{40.0f, 1.875f, 24.0f}));
    REQUIRE_VEC3_EQ(Vector3Transform(Vector3{-MESH_HALF, 0, -MESH_HALF}, m), (Vector3{32.0f, 1.875f, 16.0f}));
}

TEST_CASE("a chunk cut short is scaled to fit", "[water]") {
    const WaterChunk chunk{16.0f, 0.0f, 4.0f, 8.0f};
    const Matrix m = WaterView::chunk_matrix(chunk, 0.0f, MatrixIdentity());
    REQUIRE_VEC3_EQ(Vector3Transform(Vector3{-MESH_HALF, 0, -MESH_HALF}, m), (Vector3{16.0f, 0.0f, 0.0f}));
    REQUIRE_VEC3_EQ(Vector3Transform(Vector3{MESH_HALF, 0, MESH_HALF}, m), (Vector3{20.0f, 0.0f, 8.0f}));
}

TEST_CASE("the water moves, turns and scales with the terrain", "[water]") {
    // The terrain put somewhere else entirely: doubled, a quarter turn about
    // up, and moved. A point on the water has to land wherever the same point
    // in the terrain's space does.
    const Matrix terrain = MatrixMultiply(MatrixMultiply(
        MatrixScale(2.0f, 2.0f, 2.0f),
        MatrixRotateY(PI / 2.0f)),
        MatrixTranslate(100.0f, 5.0f, -30.0f));

    const WaterChunk chunk{16.0f, 32.0f, 16.0f, 16.0f};
    const float surface = water_surface_height(1);
    const Matrix m = WaterView::chunk_matrix(chunk, surface, terrain);

    const Vector3 corner_in_terrain{16.0f, surface, 32.0f};
    REQUIRE_VEC3_EQ(Vector3Transform(Vector3{-MESH_HALF, 0, -MESH_HALF}, m), Vector3Transform(corner_in_terrain, terrain));
}

TEST_CASE("culling in the terrain's space follows the terrain", "[water]") {
    const auto chunks = water_chunk_layout(4 * WATER_CHUNK_SIZE, 4 * WATER_CHUNK_SIZE, WATER_CHUNK_SIZE);
    const float surface = water_surface_height(1);

    // Looking straight down at the terrain's origin, about 23 units across
    const Matrix view = MatrixLookAt(Vector3{8, 30, 8}, Vector3{8, 0, 8}, Vector3{0, 0, 1});
    const Matrix projection = MatrixPerspective(45.0 * DEG2RAD, 1.0, 0.1, 100.0);
    const Matrix view_projection = MatrixMultiply(view, projection);

    // With the terrain at the origin the first chunk is under the camera
    const Frustum at_origin = frustum_from_matrix(MatrixMultiply(MatrixIdentity(), view_projection));
    REQUIRE(frustum_contains_box(at_origin, water_chunk_bounds(chunks[0], surface)));

    // With the terrain moved a long way off none of it is, though the chunks
    // themselves have not changed
    const Matrix moved = MatrixTranslate(1000.0f, 0.0f, 0.0f);
    const Frustum after_move = frustum_from_matrix(MatrixMultiply(moved, view_projection));
    for (const WaterChunk& c : chunks) {
        REQUIRE_FALSE(frustum_contains_box(after_move, water_chunk_bounds(c, surface)));
    }

    // Slid two chunks the other way, the chunk under the camera is the third
    // one along the first row
    const Matrix shifted = MatrixTranslate(-2.0f * WATER_CHUNK_SIZE, 0.0f, 0.0f);
    const Frustum shifted_f = frustum_from_matrix(MatrixMultiply(shifted, view_projection));
    REQUIRE_FALSE(frustum_contains_box(shifted_f, water_chunk_bounds(chunks[0], surface)));
    REQUIRE(frustum_contains_box(shifted_f, water_chunk_bounds(chunks[2], surface)));
}
