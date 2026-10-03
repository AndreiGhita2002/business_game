// Routes: which point lists make one, and where a distance along one lands.

#include <catch2/catch_test_macros.hpp>

#include "sim/Routes.hpp"

using namespace sim;

namespace {

Point pt(const int64_t x, const int64_t y, const int64_t z = 0) {
    return Point{Fixed::from_int(x), Fixed::from_int(y), Fixed::from_int(z)};
}

// A 10 by 4 rectangle, anticlockwise from the origin. 28 long.
std::vector<Point> rectangle() {
    return {pt(0, 0), pt(10, 0), pt(10, 4), pt(0, 4)};
}

} // namespace

TEST_CASE("Routes: what makes a valid route", "[sim][routes]") {
    REQUIRE(Routes::is_valid(rectangle()));
    // Two points: there and back along the same line
    REQUIRE(Routes::is_valid({pt(0, 0), pt(5, 0)}));
    // Points along a straight side are fine, they only split a segment
    REQUIRE(Routes::is_valid({pt(0, 0), pt(5, 0), pt(10, 0), pt(10, 4), pt(0, 4)}));
    // Height changes are free
    REQUIRE(Routes::is_valid({pt(0, 0, 0), pt(10, 0, 3), pt(10, 4, 1), pt(0, 4, 2)}));

    REQUIRE_FALSE(Routes::is_valid({}));
    REQUIRE_FALSE(Routes::is_valid({pt(0, 0)}));
    // A diagonal segment
    REQUIRE_FALSE(Routes::is_valid({pt(0, 0), pt(10, 0), pt(0, 4)}));
    // A segment with no length on the ground, even if it climbs
    REQUIRE_FALSE(Routes::is_valid({pt(0, 0, 0), pt(0, 0, 5), pt(10, 0)}));
}

TEST_CASE("Routes: length includes the closing segment", "[sim][routes]") {
    Routes routes;
    const auto id = routes.add(rectangle());
    REQUIRE(id.has_value());
    REQUIRE(routes.get(*id)->length() == Fixed::from_int(28));
    REQUIRE_FALSE(routes.add({pt(0, 0), pt(1, 1)}).has_value());
}

TEST_CASE("Routes: pose at corners and along segments", "[sim][routes]") {
    Routes routes;
    const RouteId id = *routes.add(rectangle());

    const RoutePose start = *routes.pose_at(id, Fixed::from_int(0));
    REQUIRE(start.position == pt(0, 0));
    REQUIRE(start.dir_x == 1);
    REQUIRE(start.dir_y == 0);
    REQUIRE(start.segment == 0);

    const RoutePose mid = *routes.pose_at(id, Fixed::from_ratio(5, 2));
    REQUIRE(mid.position == Point{Fixed::from_ratio(5, 2), Fixed{}, Fixed{}});

    // Exactly on the corner belongs to the segment leaving it
    const RoutePose corner = *routes.pose_at(id, Fixed::from_int(10));
    REQUIRE(corner.position == pt(10, 0));
    REQUIRE(corner.segment == 1);
    REQUIRE(corner.dir_x == 0);
    REQUIRE(corner.dir_y == 1);

    // One unit into the closing segment, which runs from (0, 4) back down to the start
    const RoutePose closing = *routes.pose_at(id, Fixed::from_int(25));
    REQUIRE(closing.position == pt(0, 3));
    REQUIRE(closing.dir_x == 0);
    REQUIRE(closing.dir_y == -1);
}

TEST_CASE("Routes: distances wrap onto the loop both ways", "[sim][routes]") {
    Routes routes;
    const RouteId id = *routes.add(rectangle());

    REQUIRE(routes.pose_at(id, Fixed::from_int(28))->position == pt(0, 0));
    REQUIRE(routes.pose_at(id, Fixed::from_int(30))->position == pt(2, 0));
    REQUIRE(routes.pose_at(id, Fixed::from_int(-1))->position == pt(0, 1));
}

TEST_CASE("Routes: height is interpolated along a segment", "[sim][routes]") {
    Routes routes;
    const RouteId id = *routes.add({pt(0, 0, 0), pt(8, 0, 4), pt(8, 2, 4), pt(0, 2, 0)});

    REQUIRE(routes.pose_at(id, Fixed::from_int(2))->position.z == Fixed::from_int(1));
    REQUIRE(routes.pose_at(id, Fixed::from_int(4))->position.z == Fixed::from_int(2));
    REQUIRE(routes.pose_at(id, Fixed::from_int(9))->position.z == Fixed::from_int(4));
    // Halfway down the third segment, from (8,2,4) to (0,2,0)
    REQUIRE(routes.pose_at(id, Fixed::from_int(14))->position.z == Fixed::from_int(2));
}

TEST_CASE("Routes: an unknown route has no pose", "[sim][routes]") {
    Routes routes;
    REQUIRE_FALSE(routes.pose_at(RouteId{}, Fixed{}).has_value());
}
