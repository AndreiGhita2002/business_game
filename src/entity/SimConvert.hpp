//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIMCONVERT_HPP
#define BUSINESS_GAME_SIMCONVERT_HPP
#include <raylib.h>

#include "sim/Fixed.hpp"
#include "sim/Routes.hpp"

/**
 * Turning simulation values into things that can be drawn. One direction only:
 * nothing on the presentation side is ever turned back into simulation state.
 *
 * Simulation space is grid space: x and y on the ground, z up, one unit per
 * terrain voxel. World space is the mesher's: X is grid x, Y is grid z (up),
 * Z is grid y. That assumes the map sits at the world origin unturned, which
 * it does; should the map ever move, its transform belongs in sim_to_world().
 */

/** Seconds of game time per simulation tick. */
float tick_seconds();

float to_float(sim::Fixed value);

/** A simulation point in world space. */
Vector3 sim_to_world(const sim::Point& point);

/**
 * The rotation about world Y that turns a model facing +X (grid x) to face
 * along a ground direction given in simulation axes.
 */
Quaternion heading_rotation(int dir_x, int dir_y);

/** Where something is drawn, and which way it faces. */
struct Pose {
    Vector3 position{0.0f, 0.0f, 0.0f};
    Quaternion rotation{0.0f, 0.0f, 0.0f, 1.0f};
};

/**
 * A vehicle's pose from its place on a route. `reversed` turns it round, for
 * a vehicle running the route backwards, so it faces the way it is going.
 */
Pose pose_from_route(const sim::RoutePose& route_pose, bool reversed);

/**
 * Between two ticks' poses. `alpha` is how far through the tick the frame is,
 * 0 at the older pose and 1 at the newer.
 */
Pose interpolate_pose(const Pose& from, const Pose& to, float alpha);

#endif //BUSINESS_GAME_SIMCONVERT_HPP
