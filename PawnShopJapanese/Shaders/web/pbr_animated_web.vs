#version 300 es
precision highp float;
precision highp int;

#define MAX_BONE_NUM 128
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
uniform mat4 customBoneMatrices[MAX_BONE_NUM];
uniform int boneCount;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec2 fragShadowTexCoord;
out float fragShadowDepth;
out mat3 TBN;

void main()
{
ivec4 boneIndex = ivec4(floor(vertexBoneIndices + vec4(0.5)));
vec4 boneWeight = max(vertexBoneWeights, vec4(0.0));

// Do not clamp bad indices to 127.
// If an index is outside the real uploaded bone count, kill that influence.
if (boneIndex.x < 0 || boneIndex.x >= boneCount) boneWeight.x = 0.0;
if (boneIndex.y < 0 || boneIndex.y >= boneCount) boneWeight.y = 0.0;
if (boneIndex.z < 0 || boneIndex.z >= boneCount) boneWeight.z = 0.0;
if (boneIndex.w < 0 || boneIndex.w >= boneCount) boneWeight.w = 0.0;

// Now safe to clamp only after invalid weights are removed.
boneIndex = clamp(
    boneIndex,
    ivec4(0),
    ivec4(MAX_BONE_NUM - 1)
);

float weightSum =
    boneWeight.x +
    boneWeight.y +
    boneWeight.z +
    boneWeight.w;

if (weightSum > 0.0001)
{
    boneWeight /= weightSum;
}
else
{
    boneIndex = ivec4(0, 0, 0, 0);
    boneWeight = vec4(1.0, 0.0, 0.0, 0.0);
}

mat4 skinMat =
      customBoneMatrices[boneIndex.x] * boneWeight.x
    + customBoneMatrices[boneIndex.y] * boneWeight.y
    + customBoneMatrices[boneIndex.z] * boneWeight.z
    + customBoneMatrices[boneIndex.w] * boneWeight.w;

    vec4 skinnedLocalPos = skinMat * vec4(vertexPosition, 1.0);
    vec4 skinnedLocalNormal = skinMat * vec4(vertexNormal, 0.0);
    vec4 skinnedLocalTangent = skinMat * vec4(vertexTangent.xyz, 0.0);

    mat3 normalMatrix = transpose(inverse(mat3(matModel)));

    vec3 N = normalize(normalMatrix * skinnedLocalNormal.xyz);
    vec3 T = normalize(normalMatrix * skinnedLocalTangent.xyz);

    T = normalize(T - dot(T, N) * N);

    vec3 B = cross(N, T) * vertexTangent.w;

    fragNormal = N;
    TBN = mat3(T, B, N);

    vec4 worldPos = matModel * skinnedLocalPos;

    fragPosition = worldPos.xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    vec4 lightClip = shadowLightVP[0] * worldPos;
    fragShadowDepth = lightClip.z / lightClip.w;
    fragShadowTexCoord = (lightClip.xy / lightClip.w) * 0.5 + 0.5;

    gl_Position = mvp * skinnedLocalPos;
}