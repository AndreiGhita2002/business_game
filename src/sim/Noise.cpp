//
// Created by Claude on 09.10.2026.
//

#include "sim/Noise.hpp"

#include <numeric>
#include <random>
#include <utility>

namespace sim {

namespace {

constexpr Fixed ONE = Fixed::from_int(1);

/** 6t^5 - 15t^4 + 10t^3, the curve that eases a cell's corners into each other. */
Fixed fade(const Fixed t) {
    return t * t * t * (t * (t * 6 - Fixed::from_int(15)) + Fixed::from_int(10));
}

Fixed lerp(const Fixed a, const Fixed b, const Fixed t) {
    return a + (b - a) * t;
}

/** The dot product of one of the twelve edge gradients, picked by the hash, with (x, y, z). */
Fixed grad(const uint8_t hash, const Fixed x, const Fixed y, const Fixed z) {
    const uint8_t h = hash & 15;
    const Fixed u = h < 8 ? x : y;
    const Fixed v = h < 4 ? y : (h == 12 || h == 14) ? x : z;
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

} // namespace

PerlinNoise::PerlinNoise(const uint32_t seed) {
    std::iota(perm.begin(), perm.end(), uint8_t{0});

    // siv's Shuffle(): every slot from the second on swaps with one at or
    // before it, picked by engine() % (i + 1). Not std::shuffle, whose choice
    // of swaps differs between standard libraries.
    std::mt19937 engine(seed);
    for (uint32_t i = 1; i < perm.size(); ++i) {
        const auto j = static_cast<uint32_t>(static_cast<uint64_t>(engine()) % (i + 1));
        std::swap(perm[i], perm[j]);
    }
}

Fixed PerlinNoise::noise3d(const Fixed x, const Fixed y, const Fixed z) const {
    // The cell the point is in, and where in the cell
    const int64_t cell_x = x.floor_int();
    const int64_t cell_y = y.floor_int();
    const int64_t cell_z = z.floor_int();
    const Fixed fx = x - Fixed::from_int(cell_x);
    const Fixed fy = y - Fixed::from_int(cell_y);
    const Fixed fz = z - Fixed::from_int(cell_z);

    // The lattice repeats every 256 cells. A negative cell wraps the same way
    // siv's `int & 255` does, as both are two's complement.
    const int ix = static_cast<int>(cell_x & 255);
    const int iy = static_cast<int>(cell_y & 255);
    const int iz = static_cast<int>(cell_z & 255);

    const Fixed u = fade(fx);
    const Fixed v = fade(fy);
    const Fixed w = fade(fz);

    // The hash of each of the cell's eight corners
    const uint8_t A = (perm[ix] + iy) & 255;
    const uint8_t B = (perm[(ix + 1) & 255] + iy) & 255;

    const uint8_t AA = (perm[A] + iz) & 255;
    const uint8_t AB = (perm[(A + 1) & 255] + iz) & 255;

    const uint8_t BA = (perm[B] + iz) & 255;
    const uint8_t BB = (perm[(B + 1) & 255] + iz) & 255;

    const Fixed p0 = grad(perm[AA], fx, fy, fz);
    const Fixed p1 = grad(perm[BA], fx - ONE, fy, fz);
    const Fixed p2 = grad(perm[AB], fx, fy - ONE, fz);
    const Fixed p3 = grad(perm[BB], fx - ONE, fy - ONE, fz);
    const Fixed p4 = grad(perm[(AA + 1) & 255], fx, fy, fz - ONE);
    const Fixed p5 = grad(perm[(BA + 1) & 255], fx - ONE, fy, fz - ONE);
    const Fixed p6 = grad(perm[(AB + 1) & 255], fx, fy - ONE, fz - ONE);
    const Fixed p7 = grad(perm[(BB + 1) & 255], fx - ONE, fy - ONE, fz - ONE);

    const Fixed q0 = lerp(p0, p1, u);
    const Fixed q1 = lerp(p2, p3, u);
    const Fixed q2 = lerp(p4, p5, u);
    const Fixed q3 = lerp(p6, p7, u);

    const Fixed r0 = lerp(q0, q1, v);
    const Fixed r1 = lerp(q2, q3, v);

    return lerp(r0, r1, w);
}

Fixed PerlinNoise::noise2d(const Fixed x, const Fixed y) const {
    return noise3d(x, y, PLANE_Z);
}

} // namespace sim
