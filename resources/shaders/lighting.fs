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
//  A ray is traced through the world's voxels first, then through whatever
//  grid volumes this draw call was given, each in its own space.
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
// order is done below, in to_voxel().
uniform mat4 worldToVolume;
// Size of the volume in voxels, so a ray knows when it has left the world
uniform ivec3 volumeSize;

// The grids that are not the map: one brick of the atlas each, one chunk on a
// side. MAX_GRID_VOLUMES is patched in alongside MAX_LIGHTS, from the constant
// of the same name in VoxelView.hpp.
#define MAX_GRID_VOLUMES x
// Must match CHUNK_SIZE in VoxelGrid.hpp: a brick holds exactly one chunk.
#define GRID_VOLUME_SIZE 16

struct GridVolume {
    mat4 worldToGrid;   // world space into this grid's own model space
    vec3 atlasOrigin;   // where its brick starts in the atlas, in voxels
};

uniform GridVolume gridVolumes[MAX_GRID_VOLUMES];
// How many of them this draw call was given. VoxelView picks them per model,
// so a fragment only ever traces the volumes that could reach it.
uniform int gridVolumeCount;
uniform sampler3D gridAtlas;

// How much of the baked ambient occlusion is taken off direct light as well as
// ambient. 0 leaves sunlit faces alone, 1 darkens them as much as shaded ones.
uniform float aoDirectStrength;

// Debug: colour every fragment by how far its shadow ray had to travel, for
// finding the fragments that cost the most. Driven by the shader menu.
uniform int debugShadowSteps;

// Output
out vec4 finalColor;

// A point in world space, in voxel coordinates of whichever volume `m` undoes.
vec3 to_voxel(mat4 m, vec3 p) {
    vec3 v = (m * vec4(p, 1.0)).xyz;
    // Model space is the mesher's - X is grid x, Y is grid z (up), Z is grid y
    // - and the texture is laid out in grid order, so the two swap back here.
    return vec3(v.x, v.z, v.y);
}

// The same for a direction, which carries no translation.
vec3 to_voxel_dir(mat4 m, vec3 d) {
    vec3 v = mat3(m) * d;
    return vec3(v.x, v.z, v.y);
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
bool march_volume(sampler3D tex, ivec3 texOrigin, ivec3 size, vec3 origin, vec3 dir, inout int steps) {
    if (size.x <= 0) return false;

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
    vec3 t_hi = (vec3(size) - origin) * inv_d;
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

    ivec3 voxel = clamp(ivec3(floor(p)), ivec3(0), size - ivec3(1));
    ivec3 step_dir = ivec3(sign(d));
    // How much t buys one whole voxel on each axis, and how much is left to the
    // first boundary from where the walk starts
    vec3 t_delta = abs(inv_d);
    vec3 t_max = (vec3(voxel) + max(vec3(step_dir), vec3(0.0)) - p) * inv_d;

    for (int i = 0; i < SHADOW_MAX_STEPS; ++i) {
        // Out of this volume: nothing left in it that could block the ray
        if (any(lessThan(voxel, ivec3(0))) || any(greaterThanEqual(voxel, size)))
            return false;

        steps += 1;
        // texOrigin is where this volume sits in its texture, which is the
        // whole texture for the world and one brick of it for a grid
        if (texelFetch(tex, texOrigin + voxel, 0).r > 0.0) return true;

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
    vec3 start = fragPosition + N * 0.01;
    vec3 toLight = -lights[i].direction;

    // The world first: the terrain is what most rays run into
    if (march_volume(worldVolume, ivec3(0), volumeSize,
                     to_voxel(worldToVolume, start),
                     to_voxel_dir(worldToVolume, toLight), steps))
        return 0.0;

    // Then the grids this draw call was handed, each traced in its own space.
    // That is what makes a turned or moving vehicle exact: its voxels are axis
    // aligned again once the ray is in there with them.
    for (int g = 0; g < MAX_GRID_VOLUMES; ++g) {
        if (g >= gridVolumeCount) break;

        mat4 m = gridVolumes[g].worldToGrid;
        if (march_volume(gridAtlas, ivec3(gridVolumes[g].atlasOrigin), ivec3(GRID_VOLUME_SIZE),
                         to_voxel(m, start), to_voxel_dir(m, toLight), steps))
            return 0.0;
    }

    return 1.0;
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
