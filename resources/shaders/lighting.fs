#version 330

// Voxel lighting.
//
// Shadows are traced, not sampled from a shadow map. The world's voxels are in
// a 3D texture, and a fragment walks that grid towards the light one voxel at a
// time: the first solid voxel it meets puts it in shadow, and running out of
// world leaves it lit. Nothing is compared against a stored depth, so there is
// no bias, no acne and no peter-panning to tune.
//
// NOTES:
//  POINT lights are lit but NOT shadowed here (visibility = 1.0).
//  MAX_LIGHTS is patched in by global::loadAndPatchShader(), so this shader
//  will not compile by itself.
//  Only the world volume is traced for now, so a grid with its own transform
//  (a vehicle) neither casts a shadow nor shadows itself yet.
// TODO(claude): a web build needs a GLSL ES 3.00 variant of this file, which
//  wants `precision highp sampler3D` and the version line changed.

// Inputs from the vertex shader
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Material/uniforms provided by raylib
uniform sampler2D texture0;   // base texture (bind a 1x1 white if untextured)
uniform vec4 colDiffuse;      // material tint

// Patched at runtime
#define MAX_LIGHTS x
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1

// How many voxels one shadow ray may cross before it gives up and calls the
// fragment lit. A ray leaves a world this shallow in a few dozen steps unless
// the light is nearly on the horizon, where the cap shows up as shadows that
// stop a long way from the camera rather than as anything wrong up close.
#define SHADOW_MAX_STEPS 256

struct Light {
    int  enabled;
    int  type;
    vec3 direction;   // the way the light travels, sky to ground. Directional only
    vec3 position;    // point lights only
    vec4 color;       // rgb in 0..1
};

uniform Light lights[MAX_LIGHTS];
uniform vec4  ambient;
uniform vec3  viewPos;   // camera position (world)

// The world's voxels, one byte each, holding the VoxelID.
uniform sampler3D worldVolume;
// World space into the volume's model space. The axis swap back into grid
// order is done below, in world_to_voxel().
uniform mat4 worldToVolume;
// Size of the volume in voxels, so a ray knows when it has left the world
uniform ivec3 volumeSize;

// How much of the baked ambient occlusion is taken off direct light as well as
// ambient. 0 leaves sunlit faces alone, 1 darkens them as much as shaded ones.
uniform float aoDirectStrength;

// Debug: colour every fragment by how far its shadow ray had to travel, for
// finding the fragments that cost the most. Driven by the shader menu.
uniform int debugShadowSteps;

// Output
out vec4 finalColor;

// A point in world space, in voxel coordinates of the volume.
vec3 world_to_voxel(vec3 p) {
    vec3 m = (worldToVolume * vec4(p, 1.0)).xyz;
    // Model space is the mesher's - X is grid x, Y is grid z (up), Z is grid y
    // - and the texture is laid out in grid order, so the two swap back here.
    return vec3(m.x, m.z, m.y);
}

// The same for a direction, which carries no translation.
vec3 world_to_voxel_dir(vec3 d) {
    vec3 m = mat3(worldToVolume) * d;
    return vec3(m.x, m.z, m.y);
}

/**
 * Walks the volume from `origin` along `dir`, both in voxel coordinates, and
 * answers whether a solid voxel blocks the way before the ray leaves the world.
 *
 * Amanatides and Woo's grid traversal: hold the distance to the next boundary
 * on each axis, and step across whichever is nearest, one voxel per step. The
 * same walk is in C++ in voxel_ray_blocked() (game/Picking.cpp), which is what
 * the unit tests cover, as a shader cannot be tested. Change the two together.
 */
bool volume_blocked(vec3 origin, vec3 dir, out int steps) {
    steps = 0;
    if (volumeSize.x <= 0) return false;

    // A component of exactly zero would divide by zero below. Nudged to
    // something tiny, the boundary on that axis lands so far away that the walk
    // never crosses it, which is what a ray parallel to an axis should do.
    const float EPS_DIR = 1e-6;
    vec3 d = vec3(
        abs(dir.x) < EPS_DIR ? EPS_DIR : dir.x,
        abs(dir.y) < EPS_DIR ? EPS_DIR : dir.y,
        abs(dir.z) < EPS_DIR ? EPS_DIR : dir.z);
    vec3 inv_d = 1.0 / d;

    // Skip the empty space in front of the volume, so that a fragment standing
    // outside it - a grid floating above the map - still reaches what is
    // inside. Ray against the volume's box, near and far.
    vec3 t_lo = (vec3(0.0) - origin) * inv_d;
    vec3 t_hi = (vec3(volumeSize) - origin) * inv_d;
    vec3 t_near = min(t_lo, t_hi);
    vec3 t_far  = max(t_lo, t_hi);
    float t_enter = max(max(t_near.x, t_near.y), t_near.z);
    float t_exit  = min(min(t_far.x,  t_far.y),  t_far.z);

    // The ray never crosses the volume at all, or only behind its start
    if (t_exit < max(t_enter, 0.0)) return false;

    // Start where the fragment stands, or just inside the box when it is
    // outside. The nudge keeps the first voxel off the boundary itself.
    float t_start = max(t_enter, 0.0) + 1e-4;
    vec3 p = origin + d * t_start;

    ivec3 voxel = clamp(ivec3(floor(p)), ivec3(0), volumeSize - ivec3(1));
    ivec3 step_dir = ivec3(sign(d));
    // How much t buys one whole voxel on each axis, and how much is left to the
    // first boundary from where the walk starts
    vec3 t_delta = abs(inv_d);
    vec3 t_max = (vec3(voxel) + max(vec3(step_dir), vec3(0.0)) - p) * inv_d;

    for (int i = 0; i < SHADOW_MAX_STEPS; ++i) {
        // Out of the world: nothing left that could block the ray
        if (any(lessThan(voxel, ivec3(0))) || any(greaterThanEqual(voxel, volumeSize)))
            return false;

        steps = i + 1;
        if (texelFetch(worldVolume, voxel, 0).r > 0.0) return true;

        // Step across the nearest boundary of the three
        if (t_max.x < t_max.y) {
            if (t_max.x < t_max.z) { voxel.x += step_dir.x; t_max.x += t_delta.x; }
            else                   { voxel.z += step_dir.z; t_max.z += t_delta.z; }
        } else {
            if (t_max.y < t_max.z) { voxel.y += step_dir.y; t_max.y += t_delta.y; }
            else                   { voxel.z += step_dir.z; t_max.z += t_delta.z; }
        }
    }
    // Ran out of steps. Calling it lit keeps the cap out of sight in the middle
    // of the scene, where a ray this long only happens with the light very low.
    return false;
}

// Whether light i reaches this fragment: 1.0 lit, 0.0 in shadow.
float light_visibility(int i, vec3 N, out int steps) {
    steps = 0;
    if (lights[i].type != LIGHT_DIRECTIONAL) return 1.0;

    // Start in the air voxel in front of the face rather than on the face
    // itself, which would be on the boundary of the solid voxel behind it. A
    // voxel normal is exact, so this nudge always lands in the right voxel at
    // any angle: it is the one constant the shadows need, and it does not want
    // tuning the way a depth bias did.
    vec3 origin = world_to_voxel(fragPosition + N * 0.01);
    vec3 dir = world_to_voxel_dir(-lights[i].direction);

    return volume_blocked(origin, dir, steps) ? 0.0 : 1.0;
}

void main() {
    // Base terms
    vec4 texelColor = texture(texture0, fragTexCoord);
    // The vertex colour carries the ambient occlusion VoxelMesher baked into
    // the mesh, 1.0 open and lower in a corner, rather than a tint of its own.
    // That is why it is no longer multiplied into the material colour here.
    float ao        = fragColor.r;
    vec4 tint       = colDiffuse;
    vec3 N          = normalize(fragNormal);
    vec3 V          = normalize(viewPos - fragPosition);

    // Accumulator for per-light contributions
    vec3 accum = vec3(0.0);
    // Only for the debug view below
    int total_steps = 0;

    // Per-light loop
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (lights[i].enabled == 0) continue;

        // Unit vector pointing FROM the fragment TOWARDS the light
        vec3 L;
        if (lights[i].type == LIGHT_DIRECTIONAL) {
            L = -lights[i].direction;
        } else {
            L = normalize(lights[i].position - fragPosition);
        }

        // A face turned away from the light needs no ray to know it is dark
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Specular (Phong), shininess = 16 (as in example)
        vec3 R = reflect(-L, N);
        float spec = pow(max(dot(V, R), 0.0), 16.0);

        // Per-light lit color before shadowing
        // finalColor_light = texelColor * ((colDiffuse + spec) * (lightColor*NdotL))
        vec3 lightColor = lights[i].color.rgb;
        vec3 perLight   = (texelColor.rgb) * ((tint.rgb + vec3(spec)) * (lightColor * NdotL));

        int steps = 0;
        float visibility = light_visibility(i, N, steps);
        total_steps += steps;

        // Occlusion is about light arriving from everywhere at once, so it
        // belongs to the ambient term. Taking a part of it off direct light as
        // well is a choice about how the voxels should look, which is what
        // aoDirectStrength is for.
        accum += perLight * visibility * mix(1.0, ao, aoDirectStrength);
    }

    // Ambient add
    vec3 ambientTerm = (texelColor.rgb * (ambient.rgb)) * tint.rgb * ao;

    // Final color
    vec3 lit = accum + ambientTerm;
    float alpha = (texelColor * tint).a;

    if (debugShadowSteps == 1) {
        // Green where the rays are short, red where they are long
        float heat = clamp(float(total_steps) / float(SHADOW_MAX_STEPS), 0.0, 1.0);
        finalColor = vec4(heat, 1.0 - heat, 0.0, 1.0);
        return;
    }

    finalColor = vec4(lit, alpha);
    finalColor = pow(finalColor, vec4(1.0 / 2.2));
}
