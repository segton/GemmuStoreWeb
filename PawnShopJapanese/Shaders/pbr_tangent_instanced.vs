#version 330

#define MAX_SHADOW_CASTERS 2

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexTangent;

in mat4 instanceTransform;

uniform mat4 mvp;
uniform mat4 shadowLightVP[MAX_SHADOW_CASTERS];

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;

out vec3 fragNormal;
out mat3 TBN;

out vec2 fragShadowTexCoord;
out float fragShadowDepth;

void main()
{
    mat4 model = instanceTransform;

    vec4 worldPos = model * vec4(vertexPosition, 1.0);
    fragPosition = worldPos.xyz;

    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec3 N = normalize(normalMatrix * vertexNormal);
    vec3 T = normalize(normalMatrix * vertexTangent.xyz);

    // Re-orthogonalize tangent against normal.
    T = normalize(T - dot(T, N) * N);

    vec3 B = normalize(cross(N, T) * vertexTangent.w);

    fragNormal = N;
    TBN = mat3(T, B, N);

    vec4 lightClip = shadowLightVP[0] * worldPos;
    fragShadowDepth = lightClip.z / lightClip.w;
    fragShadowTexCoord = (lightClip.xy / lightClip.w) * 0.5 + 0.5;

    gl_Position = mvp * worldPos;
}
