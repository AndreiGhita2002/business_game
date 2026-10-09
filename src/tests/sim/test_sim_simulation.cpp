// Simulation::step: commands going in, vehicles moving, events coming out.

#include <catch2/catch_test_macros.hpp>

#include <variant>

#include "sim/Simulation.hpp"

using namespace sim;

namespace {

Point pt(const int64_t x, const int64_t y, const int64_t z = 0) {
    return Point{Fixed::from_int(x), Fixed::from_int(y), Fixed::from_int(z)};
}

/** Submits one command and runs one tick with it, the way the game loop does. */
void step_with(Simulation& sim, CommandQueue& queue, std::unique_ptr<Command> command) {
    queue.submit(std::move(command));
    const auto commands = queue.take(sim.tick());
    sim.step(commands);
}

void step_empty(Simulation& sim, const int ticks = 1) {
    for (int i = 0; i < ticks; ++i) sim.step({});
}

template <typename E>
std::vector<E> events_of(const Simulation& sim) {
    std::vector<E> out;
    for (const Event& e : sim.events()) {
        if (const E* typed = std::get_if<E>(&e)) out.push_back(*typed);
    }
    return out;
}

/** A 10 by 4 rectangle (28 long) and the id it was given. */
RouteId add_rectangle(Simulation& sim, CommandQueue& queue) {
    step_with(sim, queue, std::make_unique<AddRoute>(
        std::vector<Point>{pt(0, 0), pt(10, 0), pt(10, 4), pt(0, 4)}));
    const auto added = events_of<RouteAdded>(sim);
    REQUIRE(added.size() == 1);
    return added[0].id;
}

VehicleId spawn(Simulation& sim, CommandQueue& queue, const RouteId route, const Fixed distance,
                const Fixed speed) {
    step_with(sim, queue, std::make_unique<SpawnVehicle>(route, distance, speed, "car"));
    const auto spawned = events_of<VehicleSpawned>(sim);
    REQUIRE(spawned.size() == 1);
    return spawned[0].id;
}

} // namespace

TEST_CASE("Simulation: ticks count up", "[sim][simulation]") {
    Simulation sim(1);
    REQUIRE(sim.tick() == 0);
    step_empty(sim, 3);
    REQUIRE(sim.tick() == 3);
}

TEST_CASE("Simulation: a spawned vehicle exists from its tick and drives", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);

    // Spawned at 2 with speed 1/2: the spawn tick already moves it once
    const VehicleId car = spawn(sim, queue, route, Fixed::from_int(2), Fixed::from_ratio(1, 2));
    REQUIRE(sim.vehicles().contains(car));
    REQUIRE(sim.vehicles().get(car)->distance == Fixed::from_ratio(5, 2));
    REQUIRE(sim.vehicles().get(car)->model == "car");

    step_empty(sim, 4);
    REQUIRE(sim.vehicles().get(car)->distance == Fixed::from_ratio(9, 2));
    REQUIRE(sim.vehicle_pose(car)->position == Point{Fixed::from_ratio(9, 2), Fixed{}, Fixed{}});
}

TEST_CASE("Simulation: vehicles wrap round the loop, forwards and backwards", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);

    const VehicleId forward = spawn(sim, queue, route, Fixed::from_int(26), Fixed::from_int(1));
    REQUIRE(sim.vehicles().get(forward)->distance == Fixed::from_int(27));
    step_empty(sim, 2);
    REQUIRE(sim.vehicles().get(forward)->distance == Fixed::from_int(1));

    const VehicleId backward = spawn(sim, queue, route, Fixed::from_int(1), Fixed::from_int(-1));
    REQUIRE(sim.vehicles().get(backward)->distance == Fixed::from_int(0));
    step_empty(sim);
    REQUIRE(sim.vehicles().get(backward)->distance == Fixed::from_int(27));
}

TEST_CASE("Simulation: a spawn distance is wrapped onto the route", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);
    const VehicleId car = spawn(sim, queue, route, Fixed::from_int(30), Fixed{});
    REQUIRE(sim.vehicles().get(car)->distance == Fixed::from_int(2));
}

TEST_CASE("Simulation: changing speed takes effect on its tick", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);
    const VehicleId car = spawn(sim, queue, route, Fixed{}, Fixed::from_int(1));

    step_with(sim, queue, std::make_unique<SetVehicleSpeed>(car, Fixed::from_int(3)));
    REQUIRE(sim.vehicles().get(car)->speed == Fixed::from_int(3));
    REQUIRE(sim.vehicles().get(car)->distance == Fixed::from_int(4));
}

TEST_CASE("Simulation: despawning removes the vehicle and says so", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);
    const VehicleId car = spawn(sim, queue, route, Fixed{}, Fixed::from_int(1));

    step_with(sim, queue, std::make_unique<DespawnVehicle>(car));
    REQUIRE_FALSE(sim.vehicles().contains(car));
    REQUIRE_FALSE(sim.vehicle_pose(car).has_value());
    const auto despawned = events_of<VehicleDespawned>(sim);
    REQUIRE(despawned.size() == 1);
    REQUIRE(despawned[0].id == car);
}

TEST_CASE("Simulation: bad commands are refused with a reason", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);
    const VehicleId car = spawn(sim, queue, route, Fixed{}, Fixed::from_int(1));
    step_with(sim, queue, std::make_unique<DespawnVehicle>(car));

    struct Case { std::unique_ptr<Command> command; RejectReason reason; };
    std::vector<Case> cases;
    cases.push_back({std::make_unique<AddRoute>(std::vector<Point>{pt(0, 0), pt(3, 3)}),
                     RejectReason::InvalidRoute});
    cases.push_back({std::make_unique<SpawnVehicle>(RouteId{}, Fixed{}, Fixed{}, "car"),
                     RejectReason::UnknownRoute});
    cases.push_back({std::make_unique<SpawnVehicle>(route, Fixed{}, Fixed{}, ""),
                     RejectReason::InvalidAsset});
    // The car is gone, so its handle is stale
    cases.push_back({std::make_unique<DespawnVehicle>(car), RejectReason::UnknownVehicle});
    cases.push_back({std::make_unique<SetVehicleSpeed>(car, Fixed{}), RejectReason::UnknownVehicle});

    for (Case& c : cases) {
        const uint32_t sequence = queue.submit(std::move(c.command));
        const auto commands = queue.take(sim.tick());
        const uint64_t before = sim.vehicles().size();
        sim.step(commands);

        const auto rejected = events_of<CommandRejected>(sim);
        REQUIRE(rejected.size() == 1);
        REQUIRE(rejected[0].reason == c.reason);
        REQUIRE(rejected[0].sequence == sequence);
        REQUIRE(sim.vehicles().size() == before);
    }
}

TEST_CASE("Simulation: a command stamped for another tick is refused", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    queue.submit(std::make_unique<AddRoute>(std::vector<Point>{pt(0, 0), pt(10, 0)}));
    const auto commands = queue.take(sim.tick() + 5);
    sim.step(commands);

    REQUIRE(sim.routes().size() == 0);
    const auto rejected = events_of<CommandRejected>(sim);
    REQUIRE(rejected.size() == 1);
    REQUIRE(rejected[0].reason == RejectReason::WrongTick);
}

TEST_CASE("Simulation: commands run in (player, sequence) order", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    const RouteId route = add_rectangle(sim, queue);

    // Handed over out of order: the spawn with the higher sequence must still
    // come second, so its vehicle takes the second slot
    std::vector<StampedCommand> commands;
    commands.push_back({sim.tick(), 0, 11, std::make_unique<SpawnVehicle>(route, Fixed{}, Fixed{}, "second")});
    commands.push_back({sim.tick(), 0, 10, std::make_unique<SpawnVehicle>(route, Fixed{}, Fixed{}, "first")});
    sim.step(commands);

    const auto spawned = events_of<VehicleSpawned>(sim);
    REQUIRE(spawned.size() == 2);
    REQUIRE(sim.vehicles().get(spawned[0].id)->model == "first");
    REQUIRE(sim.vehicles().get(spawned[1].id)->model == "second");
}

TEST_CASE("Simulation: events last for one step only", "[sim][simulation]") {
    Simulation sim(1);
    CommandQueue queue;
    add_rectangle(sim, queue);
    REQUIRE(sim.events().size() == 1);
    step_empty(sim);
    REQUIRE(sim.events().empty());
}

TEST_CASE("CommandQueue: stamps on take and empties", "[sim][commands]") {
    CommandQueue queue;
    const uint32_t a = queue.submit(std::make_unique<DespawnVehicle>(VehicleId{}), 0);
    const uint32_t b = queue.submit(std::make_unique<DespawnVehicle>(VehicleId{}), 0);
    REQUIRE(b == a + 1);
    REQUIRE(queue.pending() == 2);

    const auto taken = queue.take(7);
    REQUIRE(queue.pending() == 0);
    REQUIRE(taken.size() == 2);
    REQUIRE(taken[0].tick == 7);
    REQUIRE(taken[1].tick == 7);
    REQUIRE(taken[0].sequence == a);
    REQUIRE(queue.take(8).empty());
}

TEST_CASE("Simulation: the water starts at the default level", "[sim][simulation][water]") {
    const Simulation sim(1);
    REQUIRE(sim.water_level() == DEFAULT_WATER_LEVEL);
}

TEST_CASE("Simulation: SetWaterLevel moves the water", "[sim][simulation][water]") {
    Simulation sim(1);
    CommandQueue queue;

    step_with(sim, queue, std::make_unique<SetWaterLevel>(5));
    REQUIRE(sim.water_level() == 5);
    REQUIRE(events_of<CommandRejected>(sim).empty());

    // Down to the bottom of the world is allowed
    step_with(sim, queue, std::make_unique<SetWaterLevel>(MIN_WATER_LEVEL));
    REQUIRE(sim.water_level() == MIN_WATER_LEVEL);
}

TEST_CASE("Simulation: a water level below the world is refused", "[sim][simulation][water]") {
    Simulation sim(1);
    CommandQueue queue;

    step_with(sim, queue, std::make_unique<SetWaterLevel>(MIN_WATER_LEVEL - 1));
    REQUIRE(sim.water_level() == DEFAULT_WATER_LEVEL);
    const auto rejected = events_of<CommandRejected>(sim);
    REQUIRE(rejected.size() == 1);
    REQUIRE(rejected[0].reason == RejectReason::InvalidWaterLevel);
}

TEST_CASE("Simulation: SetWaterLevel survives being written to bytes", "[sim][simulation][water]") {
    StampedCommand c{3, 0, 0, std::make_unique<SetWaterLevel>(-7)};
    ByteWriter out;
    write_command(out, c);

    ByteReader in(out.data());
    const std::optional<StampedCommand> back = read_command(in);
    REQUIRE(back.has_value());
    REQUIRE(back->command->type() == CommandType::SetWaterLevel);
    REQUIRE(static_cast<const SetWaterLevel&>(*back->command).level == -7);
}

TEST_CASE("Simulation: the checksum notices the water level", "[sim][simulation][water]") {
    Simulation a(1), b(1);
    CommandQueue queue;
    step_with(a, queue, std::make_unique<SetWaterLevel>(DEFAULT_WATER_LEVEL + 1));
    b.step({});
    REQUIRE(a.tick() == b.tick());
    REQUIRE(a.checksum() != b.checksum());
}
