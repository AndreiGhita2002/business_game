//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_SIM_RNG_HPP
#define BUSINESS_GAME_SIM_RNG_HPP
#include <cstdint>

#include "sim/Serial.hpp"

namespace sim {

/**
 * The simulation's random numbers: PCG32 (O'Neill's pcg32_random_r), written
 * out here rather than taken from <random>.
 *
 * std::mt19937 itself is specified exactly, but the std distributions are not,
 * so uniform_int_distribution can hand two standard libraries different
 * numbers from the same engine. Every machine in a lockstep game has to draw
 * the same numbers, so the range function is hand written as well.
 *
 * Its state is part of the simulation's state: it is saved, and it goes into
 * the checksum.
 */
class Rng {
public:
    explicit Rng(uint64_t seed = 0, uint64_t stream = 0x14057b7ef767814full) {
        state = 0;
        inc = (stream << 1u) | 1u;
        next_u32();
        state += seed;
        next_u32();
    }

    uint32_t next_u32() {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    /**
     * A number in [0, bound), without the bias a plain `% bound` has. Lemire's
     * nearly divisionless method would be faster; this is the simple rejection
     * one, which is fast enough and obviously correct. `bound` must be > 0.
     */
    uint32_t next_below(const uint32_t bound) {
        // The largest multiple of `bound` that fits, everything at or above it
        // is thrown away so every remainder is equally likely
        const uint32_t threshold = (0u - bound) % bound;
        while (true) {
            const uint32_t r = next_u32();
            if (r >= threshold) return r % bound;
        }
    }

    /** A number in [lo, hi]. */
    int32_t next_range(const int32_t lo, const int32_t hi) {
        const auto span = static_cast<uint32_t>(static_cast<int64_t>(hi) - lo + 1);
        return static_cast<int32_t>(lo + static_cast<int64_t>(next_below(span)));
    }

    void write(ByteWriter& out) const {
        out.write_u64(state);
        out.write_u64(inc);
    }

    bool read(ByteReader& in) { return in.read_u64(&state) && in.read_u64(&inc); }

private:
    uint64_t state;
    uint64_t inc;
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_RNG_HPP
