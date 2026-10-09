#version 330

// Water, vertex half. The plane is flat and every vertex is left where it is;
// this is the place to move them (waves) when the water gets more than a
// colour. See src/water/WaterView.hpp for how the plane is drawn.

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

// Input uniform values, set by raylib's DrawMesh()
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Output vertex attributes (to fragment shader). Unused by the flat colour,
// passed on for whatever the fragment half does next.
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec3 fragNormal;

void main() {
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(mat3(matNormal) * vertexNormal);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
