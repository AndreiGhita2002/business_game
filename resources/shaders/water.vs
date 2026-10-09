#version 330

// Water, vertex half. The plane is flat and the waves are made here: every
// vertex is lifted by a ripple running out from the terrain's (0, 0),
//   height = amplitude * sin(waveNumber * r - angularSpeed * time)
// where r is the vertex's distance from that point across the ground. See
// src/water/WaterView.hpp for how the plane is drawn.
//
// water_wave_height() in WaterView.cpp is the C++ twin of wave_height() below,
// which is what the tests cover, as a shader cannot be tested. Change the two
// together.

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

// Input uniform values, set by raylib's DrawMesh()
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Set by WaterView::render()
// Where this chunk is in the terrain's own space: its centre's x and z, then
// how much the shared mesh is scaled along each to fit it (1 unless it is cut
// short at the edge of the map). Turns a mesh vertex back into a terrain
// position, so the distance is measured from the terrain's origin and the
// waves move with the terrain rather than staying put in the world.
uniform vec4 chunkRect;
// Seconds, wrapped round once a wave period so it never grows large enough to
// lose precision
uniform float waveTime;
uniform float waveAmplitude;   // terrain units (voxels)
uniform float waveNumber;      // 2 pi / wavelength
uniform float waveSpeed;       // 2 pi / period, radians per second

// Output vertex attributes (to fragment shader). Unused by the flat colour,
// passed on for whatever the fragment half does next.
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

float wave_height(vec2 terrain_xz) {
    float r = length(terrain_xz);
    return waveAmplitude * sin(waveNumber * r - waveSpeed * waveTime);
}

void main() {
    // The mesh is centred on the origin, so scale and shift it back to where
    // the chunk is on the terrain
    vec2 terrain_xz = vertexPosition.xz * chunkRect.zw + chunkRect.xy;

    vec3 position = vertexPosition;
    position.y += wave_height(terrain_xz);

    // The slope of the ripple, for the normal: d(height)/dr times the
    // direction away from the origin. At the origin itself the direction is
    // undefined and the slope is taken as flat.
    float r = length(terrain_xz);
    vec2 slope = vec2(0.0);
    if (r > 0.0001) {
        float dh_dr = waveAmplitude * waveNumber * cos(waveNumber * r - waveSpeed * waveTime);
        slope = dh_dr * terrain_xz / r;
    }
    // Back into the mesh's own axes, which the chunk's scale stretches
    slope *= chunkRect.zw;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    fragPosition = vec3(matModel * vec4(position, 1.0));
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(mat3(matNormal) * normal);

    gl_Position = mvp * vec4(position, 1.0);
}
