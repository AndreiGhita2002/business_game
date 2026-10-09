//
// Created by Claude on 09.10.2026.
//

#include "TestHelpers.hpp"

#include "game/Frustum.hpp"

/**
 * Throwing boxes away against what a camera sees. Built from the same two
 * matrices BeginMode3D() would set up, combined the way rlgl combines them.
 */

namespace {

// A camera at `eye` looking at `target`, 45 degrees tall, square, seeing from
// 0.1 to 100 units out
Frustum camera_frustum(const Vector3 eye, const Vector3 target) {
    const Matrix view = MatrixLookAt(eye, target, Vector3{0.0f, 1.0f, 0.0f});
    const Matrix projection = MatrixPerspective(45.0 * DEG2RAD, 1.0, 0.1, 100.0);
    return frustum_from_matrix(MatrixMultiply(view, projection));
}

// A unit box centred on a point
BoundingBox box_at(const Vector3 c) {
    return BoundingBox{
        Vector3{c.x - 0.5f, c.y - 0.5f, c.z - 0.5f},
        Vector3{c.x + 0.5f, c.y + 0.5f, c.z + 0.5f},
    };
}

}

TEST_CASE("a box straight ahead is visible", "[frustum]") {
    const Frustum f = camera_frustum(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    REQUIRE(frustum_contains_box(f, box_at(Vector3{0, 0, -10})));
}

TEST_CASE("a box behind the camera is not", "[frustum]") {
    const Frustum f = camera_frustum(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{0, 0, 10})));
}

TEST_CASE("a box past the far plane is not", "[frustum]") {
    const Frustum f = camera_frustum(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{0, 0, -150})));
}

TEST_CASE("a box off to each side is not", "[frustum]") {
    const Frustum f = camera_frustum(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    // 45 degrees tall and square: at 10 out the view is about 4.1 either side
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{10, 0, -10})));
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{-10, 0, -10})));
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{0, 10, -10})));
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{0, -10, -10})));
}

TEST_CASE("a box only partly in view is visible", "[frustum]") {
    const Frustum f = camera_frustum(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    // Reaches from well outside on the right to the middle of the view
    const BoundingBox wide{Vector3{0, -0.5f, -10.5f}, Vector3{20, 0.5f, -9.5f}};
    REQUIRE(frustum_contains_box(f, wide));
}

TEST_CASE("a box around the camera is visible", "[frustum]") {
    const Frustum f = camera_frustum(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    REQUIRE(frustum_contains_box(f, BoundingBox{Vector3{-5, -5, -5}, Vector3{5, 5, 5}}));
}

TEST_CASE("the frustum follows the camera", "[frustum]") {
    // Moved and turned to look down the +x axis
    const Frustum f = camera_frustum(Vector3{50, 3, 50}, Vector3{60, 3, 50});
    REQUIRE(frustum_contains_box(f, box_at(Vector3{70, 3, 50})));
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{30, 3, 50})));
    REQUIRE_FALSE(frustum_contains_box(f, box_at(Vector3{0, 0, -10})));
}
