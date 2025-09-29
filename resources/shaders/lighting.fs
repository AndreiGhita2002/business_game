#version 330

// NOTES:
//  POINT lights are lit but NOT shadowed here (visibility = 1.0).
//  Some of this shader is patched by the main program.
//  This means that the shader will not work by itself.

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

struct Light {
    int  enabled;
    int  type;
    vec3 position;
    vec3 target;
    vec4 color;   // rgb in 0..1
};

uniform Light lights[MAX_LIGHTS];
uniform vec4  ambient;
uniform vec3  viewPos;   // camera position (world)

// Shadow maps + matrix (and texel size) per light
// shadowMap and lightVP are patched in global::loadAndPatchShader()
uniform sampler2D shadowMap;
uniform mat4 lightVP;
uniform int shadowMapResolution; // Side length (pixels)

// Output
out vec4 finalColor;

// Shadow bias constants (replace with your preferred tuning)
const float BIAS_BASE = 0.0002;  // slope-scale factor
const float BIAS_MIN  = 0.00002; // minimum bias
const float BIAS_EPS  = 0.00001; // small constant to reduce acne further

float SampleShadowMap(int i, vec2 uv) {
    //patched in global::loadAndPatchShader()
GetShadowMapFunction
}

mat4 getLightVP(int i) {
    //patched in global::loadAndPatchShader()
GetLightVPFunction
}

void main() {
    // Base terms
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec4 tint       = colDiffuse * fragColor;
    vec3 N          = normalize(fragNormal);
    vec3 V          = normalize(viewPos - fragPosition);

    // Accumulator for per-light contributions
    vec3 accum = vec3(0.0);

    // Per-light loop
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (lights[i].enabled == 0) continue;

        // Compute light direction at the fragment (unit vector pointing FROM fragment TOWARDS light)
        vec3 L;
        bool isDirectional = (lights[i].type == LIGHT_DIRECTIONAL);
        if (isDirectional) {
            // Example semantics: l = -lightDir, where lightDir = (target - position)
            vec3 lightDir = normalize(lights[i].target - lights[i].position);
            L = -lightDir;
        } else {
            // Point light: from fragment towards light position
            L = normalize(lights[i].position - fragPosition);
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Specular (Phong), shininess = 16 (as in example)
        vec3 R = reflect(-L, N);
        float spec = pow(max(dot(V, R), 0.0), 16.0);

        // Per-light lit color before shadowing
        // finalColor_light = texelColor * ((colDiffuse*fragColor + spec) * (lightColor*NdotL))
        vec3 lightColor = lights[i].color.rgb;
        vec3 perLight   = (texelColor.rgb) * ((tint.rgb + vec3(spec)) * (lightColor * NdotL));

        // Shadow factor
        float visibility = 1.0;

        if (isDirectional) {
            // Project fragment into light space -> NDC -> [0,1]
            vec4 fragLS = getLightVP(i) * vec4(fragPosition, 1.0);

            fragLS.xyz /= fragLS.w;
            vec3 uvz    = fragLS.xyz * 0.5 + 0.5;

            // Early out if outside light frustum
            if (uvz.x >= 0.0 && uvz.x <= 1.0 &&
                uvz.y >= 0.0 && uvz.y <= 1.0 &&
                uvz.z >= 0.0 && uvz.z <= 1.0) {

                // Slope-scaled depth bias
                float bias = max(BIAS_BASE * (1.0 - dot(N, L)), BIAS_MIN) + BIAS_EPS;

                // 3x3 PCF
                const int numSamples = 9;
                int occluded = 0;

                vec2 texelSize = vec2(1.0 / float(shadowMapResolution));
                for (int sx = -1; sx <= 1; ++sx) {
                    for (int sy = -1; sy <= 1; ++sy) {
                        float sampleDepth = SampleShadowMap(i, uvz.xy + texelSize * vec2(sx, sy));
                        if (uvz.z - bias > sampleDepth) occluded++;
                    }
                }

                float shadowFactor = float(occluded) / float(numSamples); // 0..1
                visibility = 1.0 - shadowFactor; // mix(finalColor, black, shadowFactor) == color * visibility
            }
            // else: fragment outside light frustum -> treat as lit (visibility = 1)
        }

        // Apply shadow visibility to this light and accumulate
        accum += perLight * visibility;
    }

    // Ambient add
    vec3 ambientTerm = (texelColor.rgb * (ambient.rgb)) * tint.rgb;

    // Final color
    vec3 lit = accum + ambientTerm;
    float alpha = (texelColor * tint).a;

    finalColor = vec4(lit, alpha);
    finalColor = pow(finalColor, vec4(1.0 / 2.2));
}

