#version 330

// Water, fragment half: a flat colour for now, alpha included, so the ground
// under shallow water shows through, fogged with distance like the voxels.

// Inputs from the vertex shader
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Set by WaterView::render() every frame, from WaterView::colour
uniform vec4 waterColour;
// The camera, in the world; the vertex half reads it too
uniform vec3 cameraPosition;

out vec4 finalColor;

// Distance fog, the same in lighting.fs and water.fs: everything fades into
// fogColour (display space, the frame's clear colour) between fogRange.x and
// fogRange.y world units from the camera. fog_amount() in game/Fog.hpp is the
// C++ twin: change the three together.
uniform vec3 fogColour;
uniform vec2 fogRange;

float fog_amount(float distance_to_camera) {
    if (fogRange.y <= fogRange.x) return distance_to_camera < fogRange.x ? 0.0 : 1.0;
    float t = clamp((distance_to_camera - fogRange.x) / (fogRange.y - fogRange.x), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void main() {
    // fragPosition is in the world (the vertex half's matModel), so this is
    // the same distance the voxels fog by, and water and shore fade together
    finalColor = vec4(mix(waterColour.rgb, fogColour, fog_amount(length(cameraPosition - fragPosition))),
                      waterColour.a);
}
