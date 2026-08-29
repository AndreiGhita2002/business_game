//
// Created by Andrei Ghita on 29.08.2026.
//
// The transform maths in game/Transform.hpp. All of it is pure: no window, no
// globals, nothing to set up.
//

#include <catch2/catch_test_macros.hpp>

// First, so that raylib.h is in before raymath.h
#include "tests/TestHelpers.hpp"

#include "game/Transform.hpp"

namespace {

/** A quaternion turning `degrees` about the up axis, which is Y in raylib. */
Quaternion yaw(const float degrees) {
    return QuaternionFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, degrees * DEG2RAD);
}

/** A turn about X. Two turns about different axes do not commute. */
Quaternion pitch(const float degrees) {
    return QuaternionFromAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, degrees * DEG2RAD);
}

/** A turn about Z. */
Quaternion roll(const float degrees) {
    return QuaternionFromAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, degrees * DEG2RAD);
}

Transform make_transform(const Vector3 translation, const Quaternion rotation, const Vector3 scale) {
    return Transform{translation, rotation, scale};
}

}

TEST_CASE("identity leaves a transform alone", "[transform]") {
    const Transform t = make_transform(Vector3{3.0f, -2.0f, 7.0f}, yaw(35.0f),
                                       Vector3{2.0f, 2.0f, 2.0f});

    REQUIRE_TRANSFORM_EQ(transform_transform(t, identity()), t);
    REQUIRE_TRANSFORM_EQ(transform_transform(identity(), t), t);
}

TEST_CASE("a parent's translation carries the child", "[transform]") {
    const Transform child = make_transform(Vector3{1.0f, 0.0f, 0.0f}, QuaternionIdentity(),
                                           Vector3{1.0f, 1.0f, 1.0f});
    const Transform parent = make_transform(Vector3{0.0f, 2.0f, 0.0f}, QuaternionIdentity(),
                                            Vector3{1.0f, 1.0f, 1.0f});

    const Transform world = transform_transform(child, parent);
    REQUIRE_VEC3_EQ(world.translation, (Vector3{1.0f, 2.0f, 0.0f}));
}

TEST_CASE("a parent's rotation swings the child around it", "[transform]") {
    // A quarter turn about the up axis takes +X to -Z
    const Transform child = make_transform(Vector3{1.0f, 0.0f, 0.0f}, QuaternionIdentity(),
                                           Vector3{1.0f, 1.0f, 1.0f});
    const Transform parent = make_transform(Vector3{0.0f, 0.0f, 0.0f}, yaw(90.0f),
                                            Vector3{1.0f, 1.0f, 1.0f});

    const Transform world = transform_transform(child, parent);
    REQUIRE_VEC3_EQ(world.translation, (Vector3{0.0f, 0.0f, -1.0f}));

    // The child is turned as well as moved, not merely carried
    REQUIRE_QUAT_EQ(world.rotation, yaw(90.0f));
}

TEST_CASE("a parent's scale multiplies the child's offset and size", "[transform]") {
    const Transform child = make_transform(Vector3{1.0f, 2.0f, 3.0f}, QuaternionIdentity(),
                                           Vector3{2.0f, 2.0f, 2.0f});
    const Transform parent = make_transform(Vector3{0.0f, 0.0f, 0.0f}, QuaternionIdentity(),
                                            Vector3{3.0f, 4.0f, 5.0f});

    const Transform world = transform_transform(child, parent);
    REQUIRE_VEC3_EQ(world.translation, (Vector3{3.0f, 8.0f, 15.0f}));
    REQUIRE_VEC3_EQ(world.scale, (Vector3{6.0f, 8.0f, 10.0f}));
}

TEST_CASE("the parent's rotation goes on top of the child's", "[transform]") {
    // Turns about different axes do not commute, so this is what pins the order
    // down: composing them the other way round would still be a rotation, and
    // would still pass every test that only ever turns about one axis.
    const Transform child = make_transform(Vector3{0.0f, 0.0f, 0.0f}, pitch(90.0f),
                                           Vector3{1.0f, 1.0f, 1.0f});
    const Transform parent = make_transform(Vector3{0.0f, 0.0f, 0.0f}, yaw(90.0f),
                                            Vector3{1.0f, 1.0f, 1.0f});

    const Transform world = transform_transform(child, parent);

    // The child's own turn happens first, then the parent's on top of it
    for (const Vector3 point : {Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f},
                                Vector3{0.0f, 0.0f, 1.0f}, Vector3{1.0f, 2.0f, 3.0f}}) {
        const Vector3 child_first = Vector3RotateByQuaternion(point, child.rotation);
        const Vector3 then_parent = Vector3RotateByQuaternion(child_first, parent.rotation);
        REQUIRE_VEC3_EQ(Vector3RotateByQuaternion(point, world.rotation), then_parent);
    }
}

TEST_CASE("a chain of turns about different axes composes in order", "[transform]") {
    const Transform a = make_transform(Vector3{0.0f, 0.0f, 0.0f}, roll(35.0f),
                                       Vector3{1.0f, 1.0f, 1.0f});
    const Transform b = make_transform(Vector3{0.0f, 0.0f, 0.0f}, pitch(-50.0f),
                                       Vector3{1.0f, 1.0f, 1.0f});
    const Transform c = make_transform(Vector3{0.0f, 0.0f, 0.0f}, yaw(80.0f),
                                       Vector3{1.0f, 1.0f, 1.0f});

    // A grid three deep: its own turn, then its parent's, then the root's
    const Transform world = transform_transform(transform_transform(a, b), c);

    const Vector3 point{1.0f, 2.0f, -3.0f};
    Vector3 expected = Vector3RotateByQuaternion(point, a.rotation);
    expected = Vector3RotateByQuaternion(expected, b.rotation);
    expected = Vector3RotateByQuaternion(expected, c.rotation);

    REQUIRE_VEC3_EQ(Vector3RotateByQuaternion(point, world.rotation), expected);
}

TEST_CASE("transform_relative_to undoes a turn about a different axis", "[transform]") {
    // The same reason as above: with one axis everywhere, an inverse that
    // composed the wrong way round would still come out right.
    const Transform local = make_transform(Vector3{2.0f, -1.0f, 4.0f}, pitch(25.0f),
                                           Vector3{1.0f, 1.0f, 1.0f});
    const Transform parent = make_transform(Vector3{3.0f, 1.0f, -2.0f}, yaw(70.0f),
                                            Vector3{2.0f, 2.0f, 2.0f});

    const Transform world = transform_transform(local, parent);
    REQUIRE_TRANSFORM_EQ(transform_relative_to(world, parent), local);
}

TEST_CASE("composing transforms is associative", "[transform]") {
    // Uniform scales only: a non-uniform scale under a rotation produces a
    // shear, which a translation/rotation/scale triple cannot represent, so
    // the grouping would genuinely matter there.
    const Transform a = make_transform(Vector3{1.0f, 2.0f, 3.0f}, yaw(30.0f),
                                       Vector3{2.0f, 2.0f, 2.0f});
    const Transform b = make_transform(Vector3{-4.0f, 0.5f, 1.0f}, pitch(-70.0f),
                                       Vector3{0.5f, 0.5f, 0.5f});
    const Transform c = make_transform(Vector3{0.0f, 9.0f, -2.0f}, roll(15.0f),
                                       Vector3{3.0f, 3.0f, 3.0f});

    REQUIRE_TRANSFORM_EQ(transform_transform(transform_transform(a, b), c),
                         transform_transform(a, transform_transform(b, c)));
}

TEST_CASE("transform_relative_to undoes transform_transform", "[transform]") {
    const Transform local = make_transform(Vector3{2.0f, -1.0f, 4.0f}, yaw(25.0f),
                                           Vector3{1.5f, 1.5f, 1.5f});

    SECTION("under a rotated, uniformly scaled parent") {
        const Transform parent = make_transform(Vector3{10.0f, 3.0f, -6.0f}, yaw(-40.0f),
                                                Vector3{2.0f, 2.0f, 2.0f});
        const Transform world = transform_transform(local, parent);
        REQUIRE_TRANSFORM_EQ(transform_relative_to(world, parent), local);
    }

    SECTION("under an unrotated, non-uniformly scaled parent") {
        const Transform parent = make_transform(Vector3{1.0f, 1.0f, 1.0f}, QuaternionIdentity(),
                                                Vector3{2.0f, 4.0f, 0.5f});
        const Transform world = transform_transform(local, parent);
        REQUIRE_TRANSFORM_EQ(transform_relative_to(world, parent), local);
    }

    SECTION("under the identity, the local transform is the world one") {
        REQUIRE_TRANSFORM_EQ(transform_relative_to(local, identity()), local);
    }
}

TEST_CASE("transform_relative_to survives a parent scaled to nothing", "[transform]") {
    // Dividing by the parent's scale is how the undo works, so a zero axis
    // would otherwise be an infinity. It is answered with zero instead.
    const Transform parent = make_transform(Vector3{0.0f, 0.0f, 0.0f}, QuaternionIdentity(),
                                            Vector3{0.0f, 1.0f, 1.0f});
    const Transform world = make_transform(Vector3{5.0f, 5.0f, 5.0f}, QuaternionIdentity(),
                                           Vector3{1.0f, 1.0f, 1.0f});

    const Transform local = transform_relative_to(world, parent);
    REQUIRE(local.translation.x == 0.0f);
    REQUIRE(local.scale.x == 0.0f);
    // The axes that are not degenerate still come back properly
    REQUIRE(local.translation.y == Catch::Approx(5.0f).margin(test::EPS));
}

TEST_CASE("apply_transform_trans scales, then rotates, then translates", "[transform]") {
    const Transform t = make_transform(Vector3{0.0f, 0.0f, 10.0f}, yaw(90.0f),
                                       Vector3{2.0f, 2.0f, 2.0f});

    // (1,0,0) scaled to (2,0,0), turned to (0,0,-2), then moved to (0,0,8)
    REQUIRE_VEC3_EQ(apply_transform_trans(Vector3{1.0f, 0.0f, 0.0f}, t),
                    (Vector3{0.0f, 0.0f, 8.0f}));
}

TEST_CASE("transform_to_matrix moves a point the same way apply_transform_trans does",
          "[transform]") {
    // The two are used interchangeably - the renderer takes the matrix, the
    // attachment snapping takes the point - so they have to agree.
    const Transform t = make_transform(Vector3{3.0f, -1.0f, 2.0f}, yaw(37.0f),
                                       Vector3{1.5f, 2.0f, 0.5f});
    const Matrix m = transform_to_matrix(t);

    for (const Vector3 point : {Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f},
                                Vector3{-2.0f, 3.0f, 4.5f}}) {
        REQUIRE_VEC3_EQ(Vector3Transform(point, m), apply_transform_trans(point, t));
    }
}

TEST_CASE("apply_transform_scale multiplies each axis", "[transform]") {
    const Transform t = make_transform(Vector3{9.0f, 9.0f, 9.0f}, yaw(45.0f),
                                       Vector3{2.0f, 3.0f, 4.0f});

    // Only the scale of the transform is read, the rest is ignored
    REQUIRE_VEC3_EQ(apply_transform_scale(Vector3{1.0f, 2.0f, 3.0f}, t),
                    (Vector3{2.0f, 6.0f, 12.0f}));
}

TEST_CASE("apply_transform_rot adds the quaternions rather than composing them",
          "[transform]") {
    // This pins down what the function currently does, which is NOT what the
    // name suggests: it is a component wise QuaternionAdd, where composing two
    // rotations is the QuaternionMultiply that transform_transform uses. Two
    // quarter turns come out as something that is not a half turn, and not even
    // a unit quaternion. Nothing outside apply_transform() calls it, so nothing
    // is visibly wrong in the game today. If it is changed to multiply, this
    // test is the one to update.
    const Transform t = make_transform(Vector3{0.0f, 0.0f, 0.0f}, yaw(90.0f),
                                       Vector3{1.0f, 1.0f, 1.0f});

    const Quaternion added = apply_transform_rot(yaw(90.0f), t);
    REQUIRE_QUAT_EQ(added, QuaternionAdd(yaw(90.0f), yaw(90.0f)));

    // Composing the same two turns gives a half turn, which the add does not
    REQUIRE_QUAT_EQ(QuaternionMultiply(yaw(90.0f), yaw(90.0f)), yaw(180.0f));
    REQUIRE(QuaternionLength(added) != Catch::Approx(1.0f).margin(test::EPS));
}
