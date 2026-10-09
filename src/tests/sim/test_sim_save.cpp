// Saved games: the state and the waiting commands written out and read back,
// a loaded game carrying on exactly as the original does, and every way a
// file can be wrong being refused cleanly.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <variant>

#include "sim/Save.hpp"

using namespace sim;

namespace {

Point pt(const int64_t x, const int64_t y, const int64_t z = 0) {
    return Point{Fixed::from_int(x), Fixed::from_int(y), Fixed::from_int(z)};
}

/** A game partway through: a route, some cars, one removed (so a slot is free), all ticked on. */
struct Game {
    Simulation sim{7};
    CommandQueue queue;
    RouteId route{};
    std::vector<VehicleId> cars;

    Game() {
        queue.submit(std::make_unique<AddRoute>(std::vector<Point>{
            pt(0, 0, 0), pt(30, 0, 3), pt(30, 10, 1), pt(0, 10, 2)}));
        tick();
        route = std::get<RouteAdded>(sim.events()[0]).id;

        for (int i = 0; i < 5; ++i) {
            queue.submit(std::make_unique<SpawnVehicle>(
                route, Fixed::from_int(i * 7), Fixed::from_ratio(i + 1, 3), "car"));
        }
        tick();
        for (const Event& e : sim.events()) {
            if (const auto* s = std::get_if<VehicleSpawned>(&e)) cars.push_back(s->id);
        }

        queue.submit(std::make_unique<DespawnVehicle>(cars[2]));
        tick(25);
    }

    void tick(const int count = 1) {
        for (int i = 0; i < count; ++i) sim.step(queue.take(sim.tick()));
    }
};

/** Steps a simulation and its queue together, the way the game loop does. */
void tick(Simulation& sim, CommandQueue& queue) {
    sim.step(queue.take(sim.tick()));
}

std::vector<uint8_t> save_bytes(const Game& game) {
    return write_save(game.sim, game.queue);
}

/** Overwrites the u32 at `offset`, for damaging a save on purpose. */
void poke_u32(std::vector<uint8_t>& bytes, const size_t offset, const uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

// Where things sit in a save: the 12 byte header, then the first section's
// tag, version and length, then its payload
constexpr size_t FORMAT_OFFSET = 4;
constexpr size_t FIRST_SECTION = 12;
constexpr size_t FIRST_SECTION_VERSION = FIRST_SECTION + 4;

std::filesystem::path temp_save_path() {
    static std::atomic<int> counter{0};
    return std::filesystem::temp_directory_path()
         / ("business_game_save_test_" + std::to_string(counter++)) / "game.bgsave";
}

} // namespace

TEST_CASE("Save: a loaded game is the game that was saved", "[sim][save]") {
    const Game game;
    const auto loaded = read_save(save_bytes(game));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->simulation.tick() == game.sim.tick());
    REQUIRE(loaded->simulation.vehicles().size() == game.sim.vehicles().size());
    REQUIRE(loaded->simulation.checksum() == game.sim.checksum());
}

TEST_CASE("Save: a loaded game carries on exactly as the original does", "[sim][save]") {
    Game game;
    auto loaded = read_save(save_bytes(game));
    REQUIRE(loaded.has_value());

    // The same commands into both from here on, including ones that name
    // vehicles by handle, which only works if the handles came back intact
    for (int t = 0; t < 100; ++t) {
        if (t == 10) {
            game.queue.submit(std::make_unique<SetVehicleSpeed>(game.cars[0], Fixed::from_int(-2)));
            loaded->commands.submit(std::make_unique<SetVehicleSpeed>(game.cars[0], Fixed::from_int(-2)));
        }
        if (t == 20) {
            // Lands in the slot the removed car left, in both
            game.queue.submit(std::make_unique<SpawnVehicle>(game.route, Fixed{}, Fixed::from_int(1), "truck"));
            loaded->commands.submit(std::make_unique<SpawnVehicle>(game.route, Fixed{}, Fixed::from_int(1), "truck"));
        }
        tick(game.sim, game.queue);
        tick(loaded->simulation, loaded->commands);
        REQUIRE(loaded->simulation.checksum() == game.sim.checksum());
    }
}

TEST_CASE("Save: commands still waiting are saved and run after the load", "[sim][save]") {
    Game game;
    game.queue.submit(std::make_unique<SetVehicleSpeed>(game.cars[1], Fixed::from_int(4)));
    game.queue.submit(std::make_unique<DespawnVehicle>(game.cars[3]));
    REQUIRE(game.queue.pending() == 2);

    auto loaded = read_save(save_bytes(game));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->commands.pending() == 2);

    tick(game.sim, game.queue);
    tick(loaded->simulation, loaded->commands);
    REQUIRE(loaded->simulation.vehicles().get(game.cars[1])->speed == Fixed::from_int(4));
    REQUIRE_FALSE(loaded->simulation.vehicles().contains(game.cars[3]));
    REQUIRE(loaded->simulation.checksum() == game.sim.checksum());
}

TEST_CASE("Save: sequence numbers carry on where they left off", "[sim][save]") {
    Game game;
    auto loaded = read_save(save_bytes(game));
    REQUIRE(loaded.has_value());

    const uint32_t original = game.queue.submit(std::make_unique<DespawnVehicle>(VehicleId{}));
    const uint32_t restored = loaded->commands.submit(std::make_unique<DespawnVehicle>(VehicleId{}));
    REQUIRE(original == restored);
}

TEST_CASE("Save: an empty game round trips", "[sim][save]") {
    const Simulation sim(3);
    const CommandQueue queue;
    const auto loaded = read_save(write_save(sim, queue));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->simulation.checksum() == sim.checksum());
    REQUIRE(loaded->commands.pending() == 0);
}

TEST_CASE("Save: the same game always writes the same bytes", "[sim][save]") {
    const Game a, b;
    REQUIRE(save_bytes(a) == save_bytes(b));
    // And a loaded game writes back what it was read from
    const std::vector<uint8_t> bytes = save_bytes(a);
    const auto loaded = read_save(bytes);
    REQUIRE(write_save(loaded->simulation, loaded->commands) == bytes);
}

TEST_CASE("Save: what is not a save is refused with a reason", "[sim][save]") {
    const std::vector<uint8_t> good = save_bytes(Game{});
    std::string error;

    SECTION("empty") {
        REQUIRE_FALSE(read_save({}, &error).has_value());
    }
    SECTION("wrong magic") {
        std::vector<uint8_t> bad = good;
        bad[0] = 'X';
        REQUIRE_FALSE(read_save(bad, &error).has_value());
    }
    SECTION("another format version") {
        std::vector<uint8_t> bad = good;
        poke_u32(bad, FORMAT_OFFSET, SAVE_FORMAT_VERSION + 1);
        REQUIRE_FALSE(read_save(bad, &error).has_value());
        REQUIRE(error.find("format version") != std::string::npos);
    }
    SECTION("another version of one section") {
        std::vector<uint8_t> bad = good;
        poke_u32(bad, FIRST_SECTION_VERSION, SECTION_CORE_VERSION + 1);
        REQUIRE_FALSE(read_save(bad, &error).has_value());
        REQUIRE(error.find("CORE") != std::string::npos);
    }
    SECTION("a section this build does not know") {
        std::vector<uint8_t> bad = good;
        poke_u32(bad, FIRST_SECTION, section_tag("NEWS"));
        REQUIRE_FALSE(read_save(bad, &error).has_value());
        REQUIRE(error.find("NEWS") != std::string::npos);
    }
    SECTION("a section missing") {
        // Claims one section fewer, so the last (the commands) is left over
        // and never read as a section: refused for the leftover bytes or the
        // missing section, either way not loaded
        std::vector<uint8_t> bad = good;
        poke_u32(bad, 8, 3);
        REQUIRE_FALSE(read_save(bad, &error).has_value());
    }
    SECTION("cut short") {
        const std::vector<uint8_t> bad(good.begin(), good.end() - 5);
        REQUIRE_FALSE(read_save(bad, &error).has_value());
    }
    SECTION("bytes after the end") {
        std::vector<uint8_t> bad = good;
        bad.push_back(0);
        REQUIRE_FALSE(read_save(bad, &error).has_value());
    }

    REQUIRE_FALSE(error.empty());
}

TEST_CASE("Save: the water level is saved", "[sim][save][water]") {
    Game game;
    game.queue.submit(std::make_unique<SetWaterLevel>(6));
    game.tick();
    REQUIRE(game.sim.water_level() == 6);

    const auto loaded = read_save(save_bytes(game));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->simulation.water_level() == 6);
    REQUIRE(loaded->simulation.checksum() == game.sim.checksum());
}

TEST_CASE("Save: a water level below the world is refused", "[sim][save][water]") {
    std::vector<uint8_t> bad = save_bytes(Game{});

    // The water level is the last thing in CORE, the first section
    uint32_t core_length = 0;
    for (int i = 0; i < 4; ++i) core_length |= static_cast<uint32_t>(bad[FIRST_SECTION + 8 + i]) << (8 * i);
    poke_u32(bad, FIRST_SECTION + 12 + core_length - 4, static_cast<uint32_t>(MIN_WATER_LEVEL - 1));

    std::string error;
    REQUIRE_FALSE(read_save(bad, &error).has_value());
    REQUIRE(error.find("CORE") != std::string::npos);
}

TEST_CASE("Save: a section duplicated is refused", "[sim][save]") {
    const Simulation sim(1);
    const CommandQueue queue;
    std::vector<uint8_t> bytes = write_save(sim, queue);

    // Copy the first section onto the end and count it
    uint32_t length = 0;
    for (int i = 0; i < 4; ++i) length |= static_cast<uint32_t>(bytes[FIRST_SECTION + 8 + i]) << (8 * i);
    const std::vector<uint8_t> section(bytes.begin() + FIRST_SECTION,
                                       bytes.begin() + FIRST_SECTION + 12 + length);
    bytes.insert(bytes.end(), section.begin(), section.end());
    uint32_t count = 0;
    for (int i = 0; i < 4; ++i) count |= static_cast<uint32_t>(bytes[8 + i]) << (8 * i);
    poke_u32(bytes, 8, count + 1);

    std::string error;
    REQUIRE_FALSE(read_save(bytes, &error).has_value());
    REQUIRE(error.find("twice") != std::string::npos);
}

TEST_CASE("Save: to a file and back", "[sim][save]") {
    const Game game;
    const std::filesystem::path path = temp_save_path();
    std::string error;

    // The directory does not exist yet: saving makes it
    REQUIRE(save_to_file(path.string(), game.sim, game.queue, &error));
    // Saving again replaces the file rather than failing on it
    REQUIRE(save_to_file(path.string(), game.sim, game.queue, &error));
    REQUIRE_FALSE(std::filesystem::exists(path.string() + ".tmp"));

    const auto loaded = load_from_file(path.string(), &error);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->simulation.checksum() == game.sim.checksum());

    std::filesystem::remove_all(path.parent_path());
    REQUIRE_FALSE(load_from_file(path.string(), &error).has_value());
    REQUIRE(error.find("cannot open") != std::string::npos);
}

TEST_CASE("Pool: a free list that would hand a slot out twice is refused", "[sim][pool][save]") {
    struct Tag;
    Pool<int, Tag> pool;
    const auto a = pool.insert(1);
    pool.insert(2);
    pool.erase(a);

    ByteWriter out;
    pool.write(out, [](ByteWriter& w, const int v) { w.write_u32(static_cast<uint32_t>(v)); });
    const auto read_int = [](ByteReader& r, int* v) {
        uint32_t u = 0;
        if (!r.read_u32(&u)) return false;
        *v = static_cast<int>(u);
        return true;
    };

    // As written, it reads back
    {
        Pool<int, Tag> copy;
        ByteReader in(out.data());
        REQUIRE(copy.read(in, read_int));
        REQUIRE(copy.size() == 1);
        REQUIRE_FALSE(copy.contains(a));
    }

    // The free list's one entry pointed at the taken slot instead
    std::vector<uint8_t> bad = out.data();
    poke_u32(bad, bad.size() - 4, 1);
    Pool<int, Tag> copy;
    ByteReader in(bad);
    REQUIRE_FALSE(copy.read(in, read_int));
    REQUIRE(copy.size() == 0);
}
