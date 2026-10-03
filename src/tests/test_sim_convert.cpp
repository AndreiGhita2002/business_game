// entity/SimConvert: simulation values into world space, and the
// interpolation between two ticks.

#include "TestHelpers.hpp"

#include "entity/SimConvert.hpp"
#include "sim/Simulation.hpp"

using sim::Fixed;

TEST_CASE("SimConvert: fixed point to float", "[entity][convert]") {
    REQUIRE(to_float(Fixed::from_int(3)) == 3.0f);
    REQUIRE(to_float(Fixed::from_ratio(-5, 4)) == -1.25f);
    REQUIRE(tick_seconds() == Catch::Approx(1.0f / sim::TICKS_PER_SECOND));
}

TEST_CASE("SimConvert: simulation z is world Y", "[entity][convert]") {
    // Grid axes in, mesher axes out: X is x, Y is z (up), Z is y
    const sim::Point p{Fixed::from_int(1), Fixed::from_int(2), Fixed::from_int(3)};
    REQUIRE_VEC3_EQ(sim_to_world(p), (Vector3{1.0f, 3.0f, 2.0f}));
}

TEST_CASE("SimConvert: a heading turns +X onto the direction of travel", "[entity][convert]") {
    // The direction is in simulation axes, so (0, 1) is world +Z
    const struct { int dx, dy; Vector3 world; } cases[] = {
        {1, 0, {1.0f, 0.0f, 0.0f}},
        {-1, 0, {-1.0f, 0.0f, 0.0f}},
        {0, 1, {0.0f, 0.0f, 1.0f}},
        {0, -1, {0.0f, 0.0f, -1.0f}},
    };
    for (const auto& c : cases) {
        const Vector3 forward = Vector3RotateByQuaternion(Vector3{1.0f, 0.0f, 0.0f}, heading_rotation(c.dx, c.dy));
        REQUIRE_VEC3_EQ(forward, c.world);
    }
}

TEST_CASE("SimConvert: a reversed vehicle faces the other way", "[entity][convert]") {
    sim::RoutePose route_pose;
    route_pose.position = sim::Point{Fixed::from_int(4), Fixed::from_int(5), Fixed::from_int(6)};
    route_pose.dir_x = 0;
    route_pose.dir_y = 1;

    const Pose ahead = pose_from_route(route_pose, false);
    const Pose back = pose_from_route(route_pose, true);
    REQUIRE_VEC3_EQ(ahead.position, (Vector3{4.0f, 6.0f, 5.0f}));
    REQUIRE_VEC3_EQ(back.position, ahead.position);
    REQUIRE_VEC3_EQ(Vector3RotateByQuaternion(Vector3{1.0f, 0.0f, 0.0f}, back.rotation),
                    (Vector3{0.0f, 0.0f, -1.0f}));
}

TEST_CASE("SimConvert: interpolation is exact at the ends", "[entity][convert]") {
    const Pose a{Vector3{0.0f, 0.0f, 0.0f}, heading_rotation(1, 0)};
    const Pose b{Vector3{2.0f, 4.0f, -2.0f}, heading_rotation(0, 1)};

    const Pose start = interpolate_pose(a, b, 0.0f);
    const Pose end = interpolate_pose(a, b, 1.0f);
    REQUIRE(start.position.x == a.position.x);
    REQUIRE(end.position.y == b.position.y);
    REQUIRE_QUAT_EQ(end.rotation, b.rotation);

    // Out of range is held at the ends, not extrapolated
    REQUIRE_VEC3_EQ(interpolate_pose(a, b, 1.5f).position, b.position);
    REQUIRE_VEC3_EQ(interpolate_pose(a, b, -0.5f).position, a.position);
}

TEST_CASE("SimConvert: halfway is halfway, and a corner turns halfway", "[entity][convert]") {
    const Pose a{Vector3{0.0f, 0.0f, 0.0f}, heading_rotation(1, 0)};
    const Pose b{Vector3{2.0f, 4.0f, -2.0f}, heading_rotation(0, 1)};
    const Pose mid = interpolate_pose(a, b, 0.5f);

    REQUIRE_VEC3_EQ(mid.position, (Vector3{1.0f, 2.0f, -1.0f}));
    // From +X to +Z the short way, so halfway faces between the two
    const float h = std::sqrt(0.5f);
    REQUIRE_VEC3_EQ(Vector3RotateByQuaternion(Vector3{1.0f, 0.0f, 0.0f}, mid.rotation),
                    (Vector3{h, 0.0f, h}));
}
