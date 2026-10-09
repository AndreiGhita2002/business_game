//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_FOG_HPP
#define BUSINESS_GAME_FOG_HPP
#include <algorithm>
#include <raylib.h>

/**
 * Distance fog: everything drawn in 3D fades into `colour` between `start` and
 * `end` world units from the camera, which is what hides the far clip plane's
 * edge. The frame is cleared to the same colour, so fully fogged ground is
 * the sky behind it.
 *
 * One of these (global::fog) is read by both the voxel shader (lighting.fs)
 * and the water's (water.fs), every frame, so the two always fog alike.
 */
struct Fog {
    Color colour = RAYWHITE;
    float start = 1200.0f;
    float end = 3800.0f;
};

/**
 * How much fog there is `distance` world units from the camera, 0 (clear) to
 * 1 (fog only): smoothstep from `start` to `end`, so it comes in and finishes
 * gently. The C++ twin of fog_amount() in lighting.fs and water.fs: change the
 * three together.
 */
inline float fog_amount(const float distance, const float start, const float end) {
    if (end <= start) return distance < start ? 0.0f : 1.0f;
    const float t = std::clamp((distance - start) / (end - start), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

#endif //BUSINESS_GAME_FOG_HPP
