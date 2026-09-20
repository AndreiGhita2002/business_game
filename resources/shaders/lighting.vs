#version 330

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;

// Output vertex attributes (to fragment shader)
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main() {
    // Send vertex attributes to fragment shader
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    // A normal is a direction, so only the rotation and scale part of the
    // matrix applies to it. The vec4(n, 1.0) form dragged the translation in
    // with it, which skewed the normals of anything drawn away from the origin.
    fragNormal = normalize(mat3(matNormal) * vertexNormal);

    // Calculate final vertex position
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}