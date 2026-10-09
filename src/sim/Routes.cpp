//
// Created by Andrei Ghita on 04.10.2026.
//

#include "sim/Routes.hpp"

#include <algorithm>

namespace sim {

namespace {

// The length of a segment that is_valid() has already agreed to, so it runs
// along one axis and one of the two differences is zero
Fixed segment_length(const Point& a, const Point& b) {
    const Fixed dx = b.x - a.x;
    const Fixed dy = b.y - a.y;
    const Fixed ax = dx < Fixed{} ? -dx : dx;
    const Fixed ay = dy < Fixed{} ? -dy : dy;
    return ax + ay;
}

int8_t sign(const Fixed v) {
    if (v > Fixed{}) return 1;
    if (v < Fixed{}) return -1;
    return 0;
}

} // namespace

bool Routes::is_valid(const std::vector<Point>& points) {
    if (points.size() < 2) return false;

    for (size_t i = 0; i < points.size(); ++i) {
        const Point& a = points[i];
        const Point& b = points[(i + 1) % points.size()];
        const bool moves_x = a.x != b.x;
        const bool moves_y = a.y != b.y;
        // Exactly one: both would be a diagonal, neither a segment with no
        // length, which would leave a vehicle on it with no direction
        if (moves_x == moves_y) return false;
    }
    return true;
}

std::optional<RouteId> Routes::add(std::vector<Point> points) {
    std::optional<Route> route = build(std::move(points));
    if (!route) return std::nullopt;
    return pool.insert(std::move(*route));
}

std::optional<Route> Routes::build(std::vector<Point> points) {
    if (!is_valid(points)) return std::nullopt;

    Route route;
    route.cumulative.reserve(points.size() + 1);
    Fixed total{};
    route.cumulative.push_back(total);
    for (size_t i = 0; i < points.size(); ++i) {
        total += segment_length(points[i], points[(i + 1) % points.size()]);
        route.cumulative.push_back(total);
    }
    route.points = std::move(points);
    return route;
}

std::optional<RoutePose> Routes::pose_at(const RouteId id, const Fixed distance) const {
    const Route* route = pool.get(id);
    if (route == nullptr) return std::nullopt;
    return route_pose(*route, distance);
}

RoutePose route_pose(const Route& route, const Fixed distance) {
    const Fixed d = wrap(distance, route.length());

    // The segment is the last one starting at or before d. cumulative[0] is
    // zero and d is below the full length, so this always lands on a segment.
    const auto after = std::upper_bound(route.cumulative.begin(), route.cumulative.end(), d);
    const auto segment = static_cast<uint32_t>((after - route.cumulative.begin()) - 1);

    const Point& a = route.points[segment];
    const Point& b = route.points[(segment + 1) % route.points.size()];
    const Fixed along = d - route.cumulative[segment];
    const Fixed seg_length = route.cumulative[segment + 1] - route.cumulative[segment];

    RoutePose pose;
    pose.segment = segment;
    pose.dir_x = sign(b.x - a.x);
    pose.dir_y = sign(b.y - a.y);
    pose.position.x = a.x + along * pose.dir_x;
    pose.position.y = a.y + along * pose.dir_y;
    pose.position.z = a.z + mul_div(b.z - a.z, along, seg_length);
    return pose;
}

void Routes::write(ByteWriter& out) const {
    pool.write(out, [](ByteWriter& w, const Route& route) {
        w.write_u32(static_cast<uint32_t>(route.points.size()));
        for (const Point& p : route.points) {
            w.write_fixed(p.x);
            w.write_fixed(p.y);
            w.write_fixed(p.z);
        }
        // The cumulative lengths follow from the points, so they are not
        // written: the checksum would only be hashing the same thing twice
    });
}

bool Routes::read(ByteReader& in) {
    return pool.read(in, [](ByteReader& r, Route* route) {
        uint32_t count = 0;
        // Three fixed point numbers to a point
        if (!r.read_u32(&count) || count > r.remaining() / 24) return false;
        std::vector<Point> points(count);
        for (Point& p : points) {
            if (!r.read_fixed(&p.x) || !r.read_fixed(&p.y) || !r.read_fixed(&p.z)) return false;
        }
        // Built again rather than trusted, which also works the lengths out
        std::optional<Route> built = build(std::move(points));
        if (!built) return false;
        *route = std::move(*built);
        return true;
    });
}

} // namespace sim
