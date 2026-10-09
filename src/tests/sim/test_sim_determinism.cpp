// Determinism: the property lockstep multiplayer, replays and desync checks all
// rest on. The same seed and the same commands have to give the same state on
// every tick, and the checksum has to notice when they do not.

#include <catch2/catch_test_macros.hpp>

#include <map>

#include "sim/Simulation.hpp"

using namespace sim;

namespace {

Point pt(const int64_t x, const int64_t y, const int64_t z = 0) {
    return Point{Fixed::from_int(x), Fixed::from_int(y), Fixed::from_int(z)};
}

/**
 * A world of two by two cells of ocean. These tests take the checksum every
 * tick, and an island is a megabyte of blocks to hash each time, which a debug
 * build feels. The terrain's own place in the checksum is tested with the
 * terrain (test_sim_terrain.cpp).
 */
TerrainSettings small_world() {
    TerrainSettings settings;
    settings.cells_x = 2;
    settings.cells_y = 2;
    settings.centre_island = false;
    return settings;
}

/**
 * A short game played through a queue, recording every stamped command and
 * the checksum after every tick. Routes and vehicles come and go, and a few
 * commands are refused, so the log exercises every command type.
 */
struct Recording {
    std::vector<StampedCommand> log;
    std::vector<uint64_t> checksums;
};

Recording play(const uint64_t seed, const int ticks, const int64_t tweak_speed = 0) {
    Simulation sim(seed, small_world());
    CommandQueue queue;
    Recording rec;

    std::vector<VehicleId> cars;
    RouteId route{};

    for (int t = 0; t < ticks; ++t) {
        if (t == 0) {
            queue.submit(std::make_unique<AddRoute>(std::vector<Point>{
                pt(0, 0, 0), pt(20, 0, 2), pt(20, 12, 5), pt(0, 12, 1)}));
        }
        if (t >= 1 && t <= 6 && !route.is_null()) {
            queue.submit(std::make_unique<SpawnVehicle>(
                route, Fixed::from_int(t * 5), Fixed::from_ratio(t + tweak_speed, 3), "car"));
        }
        if (t == 10 && cars.size() > 2) {
            queue.submit(std::make_unique<SetVehicleSpeed>(cars[1], Fixed::from_ratio(-7, 4)));
        }
        if (t == 15 && cars.size() > 3) {
            queue.submit(std::make_unique<DespawnVehicle>(cars[3]));
            // Refused: the same car twice in one tick
            queue.submit(std::make_unique<DespawnVehicle>(cars[3]));
        }
        if (t == 16 && cars.size() > 0) {
            // Takes the slot the despawned car left
            queue.submit(std::make_unique<SpawnVehicle>(route, Fixed{}, Fixed::from_int(1), "truck"));
        }

        std::vector<StampedCommand> commands = queue.take(sim.tick());
        for (const StampedCommand& c : commands) rec.log.push_back(c.clone());
        sim.step(commands);

        for (const Event& e : sim.events()) {
            if (const auto* r = std::get_if<RouteAdded>(&e)) route = r->id;
            if (const auto* v = std::get_if<VehicleSpawned>(&e)) cars.push_back(v->id);
        }
        rec.checksums.push_back(sim.checksum());
    }
    return rec;
}

/** Runs a recorded log on a fresh simulation, returning the checksum after every tick. */
std::vector<uint64_t> replay(const uint64_t seed, const std::vector<StampedCommand>& log, const int ticks) {
    // Grouped by tick, as the loop would have handed them over
    std::map<uint64_t, std::vector<StampedCommand>> by_tick;
    for (const StampedCommand& c : log) by_tick[c.tick].push_back(c.clone());

    Simulation sim(seed, small_world());
    std::vector<uint64_t> checksums;
    for (int t = 0; t < ticks; ++t) {
        sim.step(by_tick[sim.tick()]);
        checksums.push_back(sim.checksum());
    }
    return checksums;
}

constexpr int TICKS = 200;

} // namespace

TEST_CASE("Determinism: the same game twice agrees on every tick", "[sim][determinism]") {
    const Recording a = play(99, TICKS);
    const Recording b = play(99, TICKS);
    REQUIRE(a.checksums == b.checksums);
}

TEST_CASE("Determinism: replaying the log reproduces every tick", "[sim][determinism]") {
    const Recording rec = play(99, TICKS);
    REQUIRE(replay(99, rec.log, TICKS) == rec.checksums);
}

TEST_CASE("Determinism: the log survives being written to bytes", "[sim][determinism]") {
    const Recording rec = play(99, TICKS);
    const std::vector<uint8_t> bytes = write_command_log(rec.log);

    const auto read_back = read_command_log(bytes);
    REQUIRE(read_back.has_value());
    REQUIRE(read_back->size() == rec.log.size());
    // Written again, it is the same bytes
    REQUIRE(write_command_log(*read_back) == bytes);
    REQUIRE(replay(99, *read_back, TICKS) == rec.checksums);
}

TEST_CASE("Determinism: a damaged log is refused, not half read", "[sim][determinism]") {
    const Recording rec = play(99, 20);
    std::vector<uint8_t> bytes = write_command_log(rec.log);

    std::vector<uint8_t> short_log(bytes.begin(), bytes.end() - 3);
    REQUIRE_FALSE(read_command_log(short_log).has_value());

    std::vector<uint8_t> long_log = bytes;
    long_log.push_back(0);
    REQUIRE_FALSE(read_command_log(long_log).has_value());
}

TEST_CASE("Determinism: the checksum covers what matters", "[sim][determinism]") {
    const Recording base = play(99, TICKS);

    // A different seed changes nothing the cars do, but the RNG state is part
    // of the game, so the checksum still has to tell the two apart
    REQUIRE(play(100, TICKS).checksums != base.checksums);

    // One speed a little different
    const Recording tweaked = play(99, TICKS, 1);
    REQUIRE(tweaked.checksums.front() == base.checksums.front());
    REQUIRE(tweaked.checksums.back() != base.checksums.back());
}

TEST_CASE("Determinism: the checksum depends on the slot bookkeeping", "[sim][determinism]") {
    // Same vehicles in the same places, reached two ways: one with a slot
    // freed and reused, one without. The next spawn would land in different
    // slots, so the two states are not the same and must not hash the same.
    Simulation a(1), b(1);
    CommandQueue qa, qb;
    const std::vector<Point> loop{pt(0, 0), pt(10, 0), pt(10, 4), pt(0, 4)};

    qa.submit(std::make_unique<AddRoute>(loop));
    qb.submit(std::make_unique<AddRoute>(loop));
    a.step(qa.take(a.tick()));
    b.step(qb.take(b.tick()));
    const RouteId route = std::get<RouteAdded>(a.events()[0]).id;

    qa.submit(std::make_unique<SpawnVehicle>(route, Fixed{}, Fixed{}, "car"));
    a.step(qa.take(a.tick()));
    const VehicleId gone = std::get<VehicleSpawned>(a.events()[0]).id;
    qa.submit(std::make_unique<DespawnVehicle>(gone));
    a.step(qa.take(a.tick()));

    b.step({});
    b.step({});
    REQUIRE(a.tick() == b.tick());
    REQUIRE(a.vehicles().size() == b.vehicles().size());
    REQUIRE(a.checksum() != b.checksum());
}
