#version 330

#define MAX_BONE_NUM 64

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec4 vertexTangent;

// GPU skinning attributes
in vec4 vertexBoneIndices;
in vec4 vertexBoneWeights;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 shadowLightVP[MAX_SHADOW_CASTERS];
uniform mat4 boneMatrices[MAX_BONE_NUM];

// Output vertex attributes
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec2 fragShadowTexCoord;
out float fragShadowDepth;
out mat3 TBN;

void main()
{
int boneIndex0 = int(vertexBoneIndices.x + 0.5);
int boneIndex1 = int(vertexBoneIndices.y + 0.5);
int boneIndex2 = int(vertexBoneIndices.z + 0.5);
int boneIndex3 = int(vertexBoneIndices.w + 0.5);

boneIndex0 = clamp(boneIndex0, 0, MAX_BONE_NUM - 1);
boneIndex1 = clamp(boneIndex1, 0, MAX_BONE_NUM - 1);
boneIndex2 = clamp(boneIndex2, 0, MAX_BONE_NUM - 1);
boneIndex3 = clamp(boneIndex3, 0, MAX_BONE_NUM - 1);

    vec4 skinnedLocalPos =
          vertexBoneWeights.x * (boneMatrices[boneIndex0] * vec4(vertexPosition, 1.0))
        + vertexBoneWeights.y * (boneMatrices[boneIndex1] * vec4(vertexPosition, 1.0))
        + vertexBoneWeights.z * (boneMatrices[boneIndex2] * vec4(vertexPosition, 1.0))
        + vertexBoneWeights.w * (boneMatrices[boneIndex3] * vec4(vertexPosition, 1.0));

    vec4 skinnedLocalNormal =
          vertexBoneWeights.x * (boneMatrices[boneIndex0] * vec4(vertexNormal, 0.0))
        + vertexBoneWeights.y * (boneMatrices[boneIndex1] * vec4(vertexNormal, 0.0))
        + vertexBoneWeights.z * (boneMatrices[boneIndex2] * vec4(vertexNormal, 0.0))
        + vertexBoneWeights.w * (boneMatrices[boneIndex3] * vec4(vertexNormal, 0.0));

    skinnedLocalNormal.w = 0.0;

    vec4 skinnedLocalTangent =
      vertexBoneWeights.x * (boneMatrices[boneIndex0] * vec4(vertexTangent.xyz, 0.0))
    + vertexBoneWeights.y * (boneMatrices[boneIndex1] * vec4(vertexTangent.xyz, 0.0))
    + vertexBoneWeights.z * (boneMatrices[boneIndex2] * vec4(vertexTangent.xyz, 0.0))
    + vertexBoneWeights.w * (boneMatrices[boneIndex3] * vec4(vertexTangent.xyz, 0.0));

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