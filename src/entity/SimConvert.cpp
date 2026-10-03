//
// Created by Andrei Ghita on 04.10.2026.
//

#include "entity/SimConvert.hpp"

#include <cmath>
#include <raymath.h>

#include "sim/Simulation.hpp"

float tick_seconds() {
    return 1.0f / static_cast<float>(sim::TICKS_PER_SECOND);
}

float to_float(const sim::Fixed value) {
    return static_cast<float>(value.raw) / static_cast<float>(sim::Fixed::ONE);
}

Vector3 sim_to_world(const sim::Point& point) {
    return Vector3{to_float(point.x), to_float(point.z), to_float(point.y)};
}

Quaternion heading_rotation(const int dir_x, const int dir_y) {
    // A turn of a about +Y takes +X to (cos a, 0, -sin a). The direction in
    // world space is (dir_x, 0, dir_y), so cos a = dir_x and sin a = -dir_y.
    const float angle = std::atan2(static_cast<float>(-dir_y), static_cast<float>(dir_x));
    return QuaternionFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, angle);
}

Pose pose_from_route(const sim::RoutePose& route_pose, const bool reversed) {
    const int sign = reversed ? -1 : 1;
    return Pose{
        sim_to_world(route_pose.position),
        heading_rotation(sign * route_pose.dir_x, sign * route_pose.dir_y),
    };
}

Pose interpolate_pose(const Pose& from, const Pose& to, const float alpha) {
    const float t = Clamp(alpha, 0.0f, 1.0f);
    // Both ends exact, rather than whatever a lerp at 0 or 1 rounds to
    if (t <= 0.0f) return from;
    if (t >= 1.0f) return to;
    return Pose{
        Vector3Lerp(from.position, to.position, t),
        // Slerp takes the short way round, so a corner turns through 90
        // degrees over the tick rather than spinning the long way
        QuaternionSlerp(from.rotation, to.rotation, t),
    };
}
