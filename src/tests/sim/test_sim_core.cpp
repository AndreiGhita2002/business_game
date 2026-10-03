// The simulation's building blocks: fixed point numbers, the pool and its
// handles, the byte writer and reader, and the random number generator.
//
// This binary links the simulation library and Catch2 only. Nothing from the
// game side is reachable from here, which is the point.

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

#include "sim/Fixed.hpp"
#include "sim/Pool.hpp"
#include "sim/Rng.hpp"
#include "sim/Serial.hpp"

using namespace sim;

namespace {
struct TestTag;
using TestPool = Pool<std::string, TestTag>;
}

TEST_CASE("Fixed: integers and ratios are exact", "[sim][fixed]") {
    REQUIRE(Fixed::from_int(3).raw == 3 * Fixed::ONE);
    REQUIRE(Fixed::from_ratio(1, 4).raw == Fixed::ONE / 4);
    REQUIRE(Fixed::from_int(2) + Fixed::from_ratio(1, 2) == Fixed::from_ratio(5, 2));
    REQUIRE(Fixed::from_int(2) - Fixed::from_int(5) == Fixed::from_int(-3));
    REQUIRE(-Fixed::from_int(4) == Fixed::from_int(-4));
}

TEST_CASE("Fixed: multiplying and dividing", "[sim][fixed]") {
    REQUIRE(Fixed::from_int(3) * 4 == Fixed::from_int(12));
    REQUIRE(Fixed::from_int(12) / 4 == Fixed::from_int(3));
    REQUIRE(Fixed::from_ratio(3, 2) * Fixed::from_ratio(1, 2) == Fixed::from_ratio(3, 4));
    REQUIRE(Fixed::from_int(-3) * Fixed::from_int(5) == Fixed::from_int(-15));

    // Far past what a 64 bit product of the raw values could hold
    REQUIRE(Fixed::from_int(100000) * Fixed::from_int(100000) == Fixed::from_int(10000000000));

    // (b - a) * t / length, the segment interpolation
    REQUIRE(mul_div(Fixed::from_int(4), Fixed::from_int(3), Fixed::from_int(6)) == Fixed::from_int(2));
}

TEST_CASE("Fixed: floor rounds towards negative infinity", "[sim][fixed]") {
    REQUIRE(Fixed::from_ratio(7, 2).floor_int() == 3);
    REQUIRE(Fixed::from_ratio(-7, 2).floor_int() == -4);
    REQUIRE(Fixed::from_int(-4).floor_int() == -4);
}

TEST_CASE("Fixed: wrap brings any value onto a loop", "[sim][fixed]") {
    const Fixed length = Fixed::from_int(10);
    REQUIRE(wrap(Fixed::from_int(3), length) == Fixed::from_int(3));
    REQUIRE(wrap(Fixed::from_int(10), length) == Fixed::from_int(0));
    REQUIRE(wrap(Fixed::from_int(23), length) == Fixed::from_int(3));
    REQUIRE(wrap(Fixed::from_int(-1), length) == Fixed::from_int(9));
    REQUIRE(wrap(Fixed::from_int(-20), length) == Fixed::from_int(0));
}

TEST_CASE("Pool: inserted values come back by handle", "[sim][pool]") {
    TestPool pool;
    const auto a = pool.insert("a");
    const auto b = pool.insert("b");

    REQUIRE(pool.size() == 2);
    REQUIRE(*pool.get(a) == "a");
    REQUIRE(*pool.get(b) == "b");
    REQUIRE_FALSE(a == b);
}

TEST_CASE("Pool: a default handle names nothing", "[sim][pool]") {
    TestPool pool;
    pool.insert("a");
    const TestPool::Id none{};
    REQUIRE(none.is_null());
    REQUIRE_FALSE(pool.contains(none));
    REQUIRE(pool.get(none) == nullptr);
}

TEST_CASE("Pool: a stale handle is refused once its slot is reused", "[sim][pool]") {
    TestPool pool;
    const auto first = pool.insert("first");
    REQUIRE(pool.erase(first));
    REQUIRE_FALSE(pool.erase(first));

    // Same slot, next occupant
    const auto second = pool.insert("second");
    REQUIRE(second.index == first.index);
    REQUIRE(second.generation != first.generation);

    REQUIRE_FALSE(pool.contains(first));
    REQUIRE(pool.get(first) == nullptr);
    REQUIRE(*pool.get(second) == "second");
}

TEST_CASE("Pool: iterates in slot order, skipping free slots", "[sim][pool]") {
    TestPool pool;
    const auto a = pool.insert("a");
    const auto b = pool.insert("b");
    pool.insert("c");
    pool.erase(b);
    pool.insert("d");   // takes b's slot, so it is visited second
    pool.erase(a);

    std::string order;
    pool.for_each([&order](TestPool::Id, const std::string& s) { order += s; });
    REQUIRE(order == "dc");
}

TEST_CASE("Serial: every type round trips little endian", "[sim][serial]") {
    ByteWriter out;
    out.write_u8(0xAB);
    out.write_u16(0x1234);
    out.write_u32(0xDEADBEEF);
    out.write_u64(0x0102030405060708ull);
    out.write_i64(-5);
    out.write_fixed(Fixed::from_ratio(-3, 2));
    out.write_string("car");

    // Little endian whatever the machine is
    REQUIRE(out.data()[1] == 0x34);
    REQUIRE(out.data()[2] == 0x12);

    ByteReader in(out.data());
    uint8_t u8; uint16_t u16; uint32_t u32; uint64_t u64; int64_t i64; Fixed f; std::string s;
    REQUIRE(in.read_u8(&u8));
    REQUIRE(in.read_u16(&u16));
    REQUIRE(in.read_u32(&u32));
    REQUIRE(in.read_u64(&u64));
    REQUIRE(in.read_i64(&i64));
    REQUIRE(in.read_fixed(&f));
    REQUIRE(in.read_string(&s));
    REQUIRE(in.at_end());

    REQUIRE(u8 == 0xAB);
    REQUIRE(u16 == 0x1234);
    REQUIRE(u32 == 0xDEADBEEF);
    REQUIRE(u64 == 0x0102030405060708ull);
    REQUIRE(i64 == -5);
    REQUIRE(f == Fixed::from_ratio(-3, 2));
    REQUIRE(s == "car");
}

TEST_CASE("Serial: a short read fails and stays failed", "[sim][serial]") {
    ByteWriter out;
    out.write_u16(7);
    ByteReader in(out.data());

    uint32_t v = 0;
    REQUIRE_FALSE(in.read_u32(&v));
    REQUIRE_FALSE(in.ok());
    uint8_t b = 0;
    REQUIRE_FALSE(in.read_u8(&b));
}

TEST_CASE("Serial: an overlong string is refused", "[sim][serial]") {
    ByteWriter out;
    out.write_string(std::string(100, 'x'));
    ByteReader in(out.data());
    std::string s;
    REQUIRE_FALSE(in.read_string(&s, 10));
}

TEST_CASE("Rng: the same seed gives the same numbers", "[sim][rng]") {
    Rng a(42), b(42), c(43);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const uint32_t x = a.next_u32();
        REQUIRE(x == b.next_u32());
        if (x != c.next_u32()) differs = true;
    }
    REQUIRE(differs);
}

TEST_CASE("Rng: pinned output, so a change to the generator is noticed", "[sim][rng]") {
    // The first number of the reference pcg32 seeded (42, 54), from O'Neill's
    // pcg32-demo. If this ever changes, every saved game and replay changes too.
    Rng rng(42, 54);
    REQUIRE(rng.next_u32() == 0xa15c02b7u);
}

TEST_CASE("Rng: ranges stay inside their bounds", "[sim][rng]") {
    Rng rng(7);
    std::set<int32_t> seen;
    for (int i = 0; i < 2000; ++i) {
        const int32_t v = rng.next_range(-3, 3);
        REQUIRE(v >= -3);
        REQUIRE(v <= 3);
        seen.insert(v);
    }
    // Every value turns up in two thousand draws
    REQUIRE(seen.size() == 7);
}
