//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_FIXED_HPP
#define BUSINESS_GAME_SIM_FIXED_HPP
#include <compare>
#include <cstdint>

namespace sim {

/**
 * A 48.16 fixed point number: the simulation's only fractional type.
 *
 * The simulation has to come out the same on every machine that runs it - a
 * native build and a web build in the same lockstep game included - and
 * hardware fractional maths is not guaranteed to round the same way across
 * compilers and platforms. Integers are, so every fractional quantity in the
 * simulation is one of these.
 *
 * One unit is one terrain voxel. That makes the presentation's conversion a
 * cast and an axis swap, see entity/SimConvert.hpp.
 *
 * Addition, subtraction and comparison are exact. Multiplying two of them goes
 * through a 128 bit intermediate where the compiler has one (clang and gcc,
 * which covers the desktop and Emscripten builds), so it holds for any value
 * the simulation has a use for.
 */
struct Fixed {
    int64_t raw = 0;

    static constexpr int FRAC_BITS = 16;
    static constexpr int64_t ONE = int64_t{1} << FRAC_BITS;

    static constexpr Fixed from_raw(const int64_t r) { return Fixed{r}; }
    static constexpr Fixed from_int(const int64_t v) { return Fixed{v * ONE}; }

    /** num / den, rounded towards zero. `from_ratio(1, 4)` is a quarter. */
    static constexpr Fixed from_ratio(const int64_t num, const int64_t den) {
        return Fixed{num * ONE / den};
    }

    /** The whole part, rounded down (towards negative infinity). */
    constexpr int64_t floor_int() const { return raw >> FRAC_BITS; }

    friend constexpr Fixed operator+(const Fixed a, const Fixed b) { return Fixed{a.raw + b.raw}; }
    friend constexpr Fixed operator-(const Fixed a, const Fixed b) { return Fixed{a.raw - b.raw}; }
    friend constexpr Fixed operator-(const Fixed a) { return Fixed{-a.raw}; }
    friend constexpr Fixed operator*(const Fixed a, const int64_t k) { return Fixed{a.raw * k}; }
    friend constexpr Fixed operator*(const int64_t k, const Fixed a) { return Fixed{a.raw * k}; }
    friend constexpr Fixed operator/(const Fixed a, const int64_t k) { return Fixed{a.raw / k}; }

    friend constexpr Fixed operator*(const Fixed a, const Fixed b) {
#if defined(__SIZEOF_INT128__)
        return Fixed{static_cast<int64_t>((static_cast<__int128>(a.raw) * b.raw) >> FRAC_BITS)};
#else
        // Without a wide type the product has to fit in 64 bits, so roughly
        // 32768 units on either side. Fine for the slice, see the note above.
        return Fixed{(a.raw * b.raw) >> FRAC_BITS};
#endif
    }

    Fixed& operator+=(const Fixed o) { raw += o.raw; return *this; }
    Fixed& operator-=(const Fixed o) { raw -= o.raw; return *this; }

    auto operator<=>(const Fixed&) const = default;
};

/**
 * a * b / c with the intermediate held wide, rounded towards zero. What
 * interpolating along a segment needs: (b - a) * t / length.
 */
constexpr Fixed mul_div(const Fixed a, const Fixed b, const Fixed c) {
#if defined(__SIZEOF_INT128__)
    return Fixed{static_cast<int64_t>(static_cast<__int128>(a.raw) * b.raw / c.raw)};
#else
    return Fixed{a.raw * b.raw / c.raw};
#endif
}

/**
 * `value` brought into [0, length), the way a distance along a closed loop
 * wraps. Works for negative values too, which a vehicle running backwards
 * produces. `length` must be positive.
 */
constexpr Fixed wrap(const Fixed value, const Fixed length) {
    int64_t m = value.raw % length.raw;
    if (m < 0) m += length.raw;
    return Fixed{m};
}

} // namespace sim

#endif //BUSINESS_GAME_SIM_FIXED_HPP
