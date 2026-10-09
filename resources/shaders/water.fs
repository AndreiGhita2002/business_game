#version 330

// Water, fragment half: a flat colour for now, alpha included, so the ground
// under shallow water shows through.

// Inputs from the vertex shader
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec3 fragNormal;

// Set by WaterView::render() every frame, from WaterView::colour
uniform vec4 waterColour;

out vec4 finalColor;

void main() {
    finalColor = waterColour;
}
