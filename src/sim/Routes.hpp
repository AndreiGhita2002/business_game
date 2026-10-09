//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_ROUTES_HPP
#define BUSINESS_GAME_SIM_ROUTES_HPP
#include <cstdint>
#include <optional>
#include <vector>

#include "sim/Fixed.hpp"
#include "sim/Handle.hpp"
#include "sim/Pool.hpp"
#include "sim/Serial.hpp"

namespace sim {

/**
 * A point in the simulation's world. The axes are the grids' axes: x and y on
 * the ground, z up, BLOCK_SIZE units to a terrain block.
 */
struct Point {
    Fixed x;
    Fixed y;
    Fixed z;

    bool operator==(const Point&) const = default;
};

/**
 * A closed loop for vehicles to drive round: the last point joins back up to
 * the first.
 *
 * Placeholder for a real road network, there to give the architecture slice
 * something that moves. Every segment runs along x or along y, never both, so
 * its length is exact without a square root - the simulation has no use for an
 * approximate one. Height is free: z is interpolated along each segment, and
 * the length is measured on the ground.
 */
struct Route {
    std::vector<Point> points;
    // The distance along the loop at each point, plus the full length as the
    // last entry, so segment i runs from cumulative[i] to cumulative[i + 1]
    std::vector<Fixed> cumulative;

    Fixed length() const { return cumulative.back(); }
};

/** Where on a route a distance lands, and which way the route runs there. */
struct RoutePose {
    Point position;
    // The direction of the segment, each -1, 0 or 1 with exactly one of them
    // non-zero. Turning that into an angle is the presentation's business.
    int8_t dir_x = 1;
    int8_t dir_y = 0;
    // Which segment the distance fell in
    uint32_t segment = 0;
};

/** Every route in the simulation. */
class Routes {
public:
    /**
     * Whether `points` make a route: at least two of them, and every segment,
     * the closing one included, running along exactly one ground axis. Heights
     * are not checked, anything goes.
     */
    static bool is_valid(const std::vector<Point>& points);

    /** A route through these points, lengths worked out, or nothing when is_valid() says no. */
    static std::optional<Route> build(std::vector<Point> points);

    /** Adds a route, or returns nothing when is_valid() says no. */
    std::optional<RouteId> add(std::vector<Point> points);

    bool contains(RouteId id) const { return pool.contains(id); }
    const Route* get(RouteId id) const { return pool.get(id); }
    size_t size() const { return pool.size(); }

    /**
     * Where `distance` along the route lands. Any distance is accepted and
     * wrapped onto the loop, negative ones included.
     * Returns nothing for a route that does not exist.
     */
    std::optional<RoutePose> pose_at(RouteId id, Fixed distance) const;

    template <typename F>
    void for_each(F&& f) const { pool.for_each(std::forward<F>(f)); }

    void write(ByteWriter& out) const;
    /** Replaces every route with what write() wrote. False on a short or invalid record. */
    bool read(ByteReader& in);

private:
    Pool<Route, RouteTag> pool;
};

/** pose_at() for a route already in hand. */
RoutePose route_pose(const Route& route, Fixed distance);

} // namespace sim

#endif //BUSINESS_GAME_SIM_ROUTES_HPP
