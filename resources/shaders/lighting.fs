#version 330

// Inputs from the vertex shader
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;

// Material/uniforms provided by raylib
uniform sampler2D texture0;   // if no texture is bound, sample will be white
uniform vec4 colDiffuse;      // material tint

// Lighting uniforms
#define MAX_LIGHTS        4
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1

struct Light {
    int  enabled;
    int  type;
    vec3 position;
    vec3 target;
    vec4 color;   // rgb in 0..1, a unused
};

uniform Light lights[MAX_LIGHTS];
uniform vec4  ambient;
uniform vec3  viewPos;   // camera position (world)

// Output
out vec4 finalColor;

void main()
{
    // Base color = texture * vertexColor * material color
    vec4 texelColor = texture(texture0, fragTexCoord);
    // If you're truly untextured, you can force white:
    // texelColor = vec4(1.0);

    vec4 tint = colDiffuse * fragColor;
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragPosition);

    vec3 diffuseSum  = vec3(0.0);
    vec3 specularSum = vec3(0.0);

    // Simple Phong/Blinn-Phong parameters
    const float shininess = 16.0;

    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (lights[i].enabled == 0) continue;

        vec3 L;
        if (lights[i].type == LIGHT_DIRECTIONAL) {
            // Directional: direction from light position -> target
            L = -normalize(lights[i].target - lights[i].position);
        } else { // LIGHT_POINT
            // Point: direction from fragment towards light
            L = normalize(lights[i].position - fragPosition);
        }

        float NdotL = max(dot(N, L), 0.0);
        diffuseSum += lights[i].color.rgb * NdotL;

        if (NdotL > 0.0) {
            // Classic Phong specular
            vec3 R = reflect(-L, N);
            float spec = pow(max(dot(V, R), 0.0), shininess);
            // White specular; tint if you want colored specular
            specularSum += spec;
        }
    }

    // Combine lighting (linear space)
    vec4 base = texelColor * tint;
    vec3 lit  = base.rgb * (diffuseSum + ambient.rgb) + specularSum;

    finalColor = vec4(lit, base.a);

    // Optional: gamma correct. Disable while debugging brightness if needed.
    finalColor = pow(finalColor, vec4(1.0/2.2));
}
