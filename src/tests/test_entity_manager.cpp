// EntityManager and VehicleEntity: entities built and torn down as vehicles
// come into and out of range, placed where the simulation says, and nothing
// flowing back the other way.
//
// The grids go into a fake sink instead of a VoxelView, and are built with a
// null view, so none of this needs a window. Nothing here calls
// update_models().

#include "TestHelpers.hpp"

#include <algorithm>
#include <set>

#include "entity/AssetRegistry.hpp"
#include "entity/EntityManager.hpp"
#include "entity/SimConvert.hpp"
#include "game/Transform.hpp"
#include "sim/Simulation.hpp"

using sim::Fixed;

namespace {

/** Records which grids are being drawn. */
class FakeSink final : public GridSink {
public:
    std::set<VoxelGrid*> drawn;
    int adds = 0;
    int removes = 0;
    // A remove for a grid that was never added, which would mean the entity
    // and the view disagree about what is drawn
    bool removed_unknown = false;

    void add_grids(const std::vector<VoxelGrid*>& grids) override {
        adds++;
        drawn.insert(grids.begin(), grids.end());
    }

    void remove_grids(const std::vector<VoxelGrid*>& grids) override {
        removes++;
        for (VoxelGrid* g : grids) {
            if (drawn.erase(g) == 0) removed_unknown = true;
        }
    }
};

/** A simulation, a queue and a manager, ticked the way the game loop does it. */
struct Fixture {
    sim::Simulation sim{1};
    sim::CommandQueue queue;
    FakeSink sink;
    AssetRegistry assets{test::make_palette()};
    EntityManager manager{&sink, &assets, nullptr};
    sim::RouteId route{};

    Fixture() {
        assets.register_builder("car", placeholder_car_builder(1), PLACEHOLDER_CAR_PIVOT);
        manager.realize_radius = 10.0f;
        manager.unrealize_radius = 15.0f;

        // A long thin loop along x: 200 there, 4 across, 200 back
        queue.submit(std::make_unique<sim::AddRoute>(std::vector<sim::Point>{
            {Fixed::from_int(0), Fixed::from_int(0), Fixed{}},
            {Fixed::from_int(200), Fixed::from_int(0), Fixed{}},
            {Fixed::from_int(200), Fixed::from_int(4), Fixed{}},
            {Fixed::from_int(0), Fixed::from_int(4), Fixed{}},
        }));
        tick();
        route = std::get<sim::RouteAdded>(sim.events()[0]).id;
    }

    void tick(const int count = 1) {
        for (int i = 0; i < count; ++i) {
            sim.step(queue.take(sim.tick()));
            manager.on_tick(sim);
        }
    }

    void present(const Vector3 camera, const float alpha = 0.0f) {
        manager.present(sim, camera, alpha, 1.0f / 60.0f);
    }

    /** Spawns a car at `distance` with `speed` per tick, returning its id. */
    sim::VehicleId spawn(const Fixed distance, const Fixed speed, const std::string& model = "car") {
        queue.submit(std::make_unique<sim::SpawnVehicle>(route, distance, speed, model));
        tick();
        for (const sim::Event& e : sim.events()) {
            if (const auto* s = std::get_if<sim::VehicleSpawned>(&e)) return s->id;
        }
        FAIL("no vehicle was spawned");
        return {};
    }

    Vector3 world_position(const sim::VehicleId id) const {
        return sim_to_world(sim.vehicle_pose(id)->position);
    }
};

const Vector3 ORIGIN{0.0f, 0.0f, 0.0f};

} // namespace

TEST_CASE("keep_realized: two radii, so the boundary does not flicker", "[entity][manager]") {
    // Out of range and not built: built only inside the smaller radius
    REQUIRE(keep_realized(false, 9.0f, 10.0f, 15.0f));
    REQUIRE_FALSE(keep_realized(false, 12.0f, 10.0f, 15.0f));
    // Built: kept until past the larger one
    REQUIRE(keep_realized(true, 12.0f, 10.0f, 15.0f));
    REQUIRE_FALSE(keep_realized(true, 16.0f, 10.0f, 15.0f));
}

TEST_CASE("EntityManager: a vehicle in range gets an entity where the simulation says", "[entity][manager]") {
    Fixture f;
    const sim::VehicleId car = f.spawn(Fixed::from_int(2), Fixed{});
    f.present(ORIGIN);

    REQUIRE(f.manager.realized_count() == 1);
    VehicleEntity* entity = f.manager.vehicle(car);
    REQUIRE(entity != nullptr);
    // A body and four wheels, all handed to the sink
    REQUIRE(entity->owned_grids().size() == 5);
    REQUIRE(f.sink.drawn.size() == 5);

    REQUIRE_VEC3_EQ(entity->shown_pose().position, f.world_position(car));
    // The pivot is what lands on that position, so the root grid sits back
    // from it by the pivot
    const Vector3 root_at = entity->root_grid()->get_transform().translation;
    REQUIRE_VEC3_EQ(root_at, Vector3Subtract(f.world_position(car), PLACEHOLDER_CAR_PIVOT));
}

TEST_CASE("EntityManager: a vehicle out of range has no entity", "[entity][manager]") {
    Fixture f;
    f.spawn(Fixed::from_int(100), Fixed{});
    f.present(ORIGIN);
    REQUIRE(f.manager.realized_count() == 0);
    REQUIRE(f.sink.drawn.empty());
}

TEST_CASE("EntityManager: a vehicle keeps driving while nobody draws it", "[entity][manager]") {
    Fixture f;
    const sim::VehicleId car = f.spawn(Fixed{}, Fixed::from_int(1));
    f.present(ORIGIN);
    REQUIRE(f.manager.vehicle(car) != nullptr);

    // Drives out past the unrealise radius
    f.tick(20);
    f.present(ORIGIN);
    REQUIRE(f.manager.vehicle(car) == nullptr);
    REQUIRE(f.sink.drawn.empty());
    REQUIRE_FALSE(f.sink.removed_unknown);

    // ...and on, with nothing drawn
    f.tick(50);
    REQUIRE(f.sim.vehicles().get(car)->distance == Fixed::from_int(71));

    // The camera goes to where it is now: it comes back exactly there
    const Vector3 now = f.world_position(car);
    f.present(now);
    VehicleEntity* entity = f.manager.vehicle(car);
    REQUIRE(entity != nullptr);
    REQUIRE_VEC3_EQ(entity->shown_pose().position, now);
    REQUIRE(f.sink.drawn.size() == 5);
}

TEST_CASE("EntityManager: drawn between the last two ticks", "[entity][manager]") {
    Fixture f;
    const sim::VehicleId car = f.spawn(Fixed::from_int(2), Fixed::from_int(1));
    f.present(ORIGIN);
    const Vector3 before = f.world_position(car);

    f.tick();
    const Vector3 after = f.world_position(car);
    REQUIRE(after.x == Catch::Approx(before.x + 1.0f));

    f.present(ORIGIN, 0.0f);
    REQUIRE_VEC3_EQ(f.manager.vehicle(car)->shown_pose().position, before);
    f.present(ORIGIN, 0.25f);
    REQUIRE(f.manager.vehicle(car)->shown_pose().position.x == Catch::Approx(before.x + 0.25f));
    f.present(ORIGIN, 1.0f);
    REQUIRE_VEC3_EQ(f.manager.vehicle(car)->shown_pose().position, after);
}

TEST_CASE("EntityManager: a despawned vehicle loses its entity on that tick", "[entity][manager]") {
    Fixture f;
    const sim::VehicleId car = f.spawn(Fixed::from_int(2), Fixed{});
    f.present(ORIGIN);
    REQUIRE(f.sink.drawn.size() == 5);

    f.queue.submit(std::make_unique<sim::DespawnVehicle>(car));
    f.tick();
    // Gone before the next present(), straight from the event
    REQUIRE(f.manager.vehicle(car) == nullptr);
    REQUIRE(f.sink.drawn.empty());
    REQUIRE_FALSE(f.sink.removed_unknown);
}

TEST_CASE("EntityManager: any of a vehicle's grids picks it", "[entity][manager]") {
    Fixture f;
    const sim::VehicleId car = f.spawn(Fixed::from_int(2), Fixed{});
    f.present(ORIGIN);

    const VehicleEntity* entity = f.manager.vehicle(car);
    for (const VoxelGrid* grid : entity->owned_grids()) {
        REQUIRE(f.manager.vehicle_for_grid(grid) == entity);
    }
    const auto stranger = test::make_chunk_grid(test::make_palette());
    REQUIRE(f.manager.vehicle_for_grid(stranger.get()) == nullptr);

    // And none of them once it is gone
    const std::vector<VoxelGrid*> grids = entity->owned_grids();
    f.queue.submit(std::make_unique<sim::DespawnVehicle>(car));
    f.tick();
    for (const VoxelGrid* grid : grids) REQUIRE(f.manager.vehicle_for_grid(grid) == nullptr);
}

TEST_CASE("EntityManager: an unknown model is skipped, not fatal", "[entity][manager]") {
    Fixture f;
    f.spawn(Fixed::from_int(2), Fixed{}, "no such model");
    f.present(ORIGIN);
    f.present(ORIGIN);
    REQUIRE(f.manager.realized_count() == 0);
    REQUIRE(f.sink.drawn.empty());
}

TEST_CASE("EntityManager: cleared entities are rebuilt from the simulation alone", "[entity][manager]") {
    Fixture f;
    const sim::VehicleId a = f.spawn(Fixed::from_int(2), Fixed{});
    const sim::VehicleId b = f.spawn(Fixed::from_int(5), Fixed{});
    f.present(ORIGIN);
    REQUIRE(f.manager.realized_count() == 2);

    f.manager.clear();
    REQUIRE(f.manager.realized_count() == 0);
    REQUIRE(f.sink.drawn.empty());

    f.present(ORIGIN);
    REQUIRE(f.manager.realized_count() == 2);
    REQUIRE_VEC3_EQ(f.manager.vehicle(a)->shown_pose().position, f.world_position(a));
    REQUIRE_VEC3_EQ(f.manager.vehicle(b)->shown_pose().position, f.world_position(b));
}

TEST_CASE("EntityManager: presenting changes nothing in the simulation", "[entity][manager]") {
    Fixture f;
    f.spawn(Fixed::from_int(2), Fixed::from_int(1));
    f.spawn(Fixed::from_int(150), Fixed::from_int(1));
    const uint64_t before = f.sim.checksum();

    for (int i = 0; i < 10; ++i) f.present(Vector3{static_cast<float>(i) * 20.0f, 0.0f, 0.0f}, 0.5f);
    f.manager.clear();
    REQUIRE(f.sim.checksum() == before);
}

TEST_CASE("WheelSpinScript: the wheels turn and stay on their axles", "[entity][wheels]") {
    Fixture f;
    const sim::VehicleId car = f.spawn(Fixed::from_int(2), Fixed::from_int(1));
    f.present(ORIGIN);
    const VehicleEntity* entity = f.manager.vehicle(car);
    REQUIRE(entity->ground_speed() == Catch::Approx(static_cast<float>(sim::TICKS_PER_SECOND)));

    const std::vector<VoxelGrid*> wheels = find_wheels(entity->owned_grids());
    REQUIRE(wheels.size() == 4);

    for (int i = 0; i < 5; ++i) f.present(ORIGIN, 0.0f);
    for (VoxelGrid* wheel : wheels) {
        REQUIRE(wheel->is_attached());
        // Turned away from where it started
        REQUIRE(std::fabs(wheel->get_transform().rotation.w) < 0.9999f);

        // The hub still sits on the axle end: their centres agree in the body's space
        const Attachment* a = wheel->get_attachment();
        const Vector3 hub = Vector3Transform(VoxelGrid::voxel_centre_local(a->local_voxel),
                                             transform_to_matrix(wheel->get_transform()));
        REQUIRE_VEC3_EQ(hub, VoxelGrid::voxel_centre_local(a->anchor_voxel));
    }
}
