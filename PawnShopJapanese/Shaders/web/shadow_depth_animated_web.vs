#version 300 es
precision highp float;
precision highp int;

#define MAX_BONE_NUM 96
#define MAX_SHADOW_CASTERS 2

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexTangent;

in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 shadowLightVP[MAX_SHADOW_CASTERS];
uniform mat4 boneMatrices[MAX_BONE_NUM];

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec2 fragShadowTexCoord;
out float fragShadowDepth;
out mat3 TBN;

void main()
{
    vec4 localPos = vec4(vertexPosition, 1.0);
    vec3 localNormal = vertexNormal;
    vec3 localTangent = vertexTangent.xyz;

    vec4 worldPos = matModel * localPos;

    fragPosition = worldPos.xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    mat3 normalMatrix = mat3(matModel);

    vec3 N = normalize(normalMatrix * localNormal);
    vec3 T = normalize(normalMatrix * localTangent);

    T = normalize(T - dot(T, N) * N);

    vec3 B = normalize(cross(N, T) * vertexTangent.w);

    fragNormal = N;
    TBN = mat3(T, B, N);

    vec4 lightClip = shadowLightVP[0] * worldPos;

    if (abs(lightClip.w) > 0.00001)
    {
        fragShadowDepth = lightClip.z / lightClip.w;
        fragShadowTexCoord = (lightClip.xy / lightClip.w) * 0.5 + 0.5;
    }
    else
    {
        fragShadowDepth = 1.0;
        fragShadowTexCoord = vec2(-1.0, -1.0);
    }

    gl_Position = mvp * localPos;
}