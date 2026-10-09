//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_SIM_NOISE_HPP
#define BUSINESS_GAME_SIM_NOISE_HPP
#include <array>
#include <cstdint>

#include "sim/Fixed.hpp"

namespace sim {

/**
 * Ken Perlin's improved noise, in Fixed.
 *
 * A port of siv::PerlinNoise (includes/PerlinNoise.hpp), which is what the
 * terrain used to be generated with on the presentation side. That one works
 * in hardware fractional maths, which the simulation cannot use, so this is
 * the same algorithm step for step with every value a Fixed instead: the same
 * permutation from the same seed, the same fade curve, the same gradients.
 * It agrees with the original to within a few 65536ths, which is the rounding
 * of a Fixed multiply, and agrees with itself exactly on every machine.
 *
 * The values come out in roughly [-1, 1].
 */
class PerlinNoise {
public:
    /**
     * The permutation siv::PerlinNoise builds from the same seed. It shuffles
     * with std::mt19937 and a plain modulo, no std distribution, and the
     * engine's output is specified exactly by the standard, so this is the
     * same on every standard library.
     */
    explicit PerlinNoise(uint32_t seed);

    Fixed noise3d(Fixed x, Fixed y, Fixed z) const;

    /**
     * siv::PerlinNoise's noise2D(): the 3D noise on a plane a little above
     * z = 0, as z = 0 is a lattice plane, where the noise is flatter.
     */
    Fixed noise2d(Fixed x, Fixed y) const;

    /** siv's SIVPERLIN_DEFAULT_Z, the plane noise2d() samples. */
    static constexpr Fixed PLANE_Z = Fixed::from_ratio(34567, 100000);

    const std::array<uint8_t, 256>& permutation() const { return perm; }

private:
    std::array<uint8_t, 256> perm{};
};

} // namespace sim

#endif //BUSINESS_GAME_SIM_NOISE_HPP
