#version 300 es
precision highp float;
precision highp int;

#define MAX_SHADOW_CASTERS 2

// Input vertex attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexTangent;
in vec4 vertexColor;

// Input uniform values
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform vec3 lightPos;
uniform vec4 difColor;

uniform mat4 shadowLightVP[MAX_SHADOW_CASTERS];

// Output vertex attributes
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out mat3 TBN;
out vec2 fragShadowTexCoord;
out float fragShadowDepth;
void main()
{
    vec3 vertexBinormal = cross(vertexNormal, vertexTangent.xyz) * vertexTangent.w;

    mat3 normalMatrix = transpose(inverse(mat3(matModel)));

    vec4 worldPos = matModel * vec4(vertexPosition, 1.0);

    fragPosition = worldPos.xyz;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;

    fragNormal = normalize(normalMatrix * vertexNormal);

    vec3 fragTangent = normalize(normalMatrix * vertexTangent.xyz);
    fragTangent = normalize(fragTangent - dot(fragTangent, fragNormal) * fragNormal);

    vec3 fragBinormal = normalize(cross(fragNormal, fragTangent) * vertexTangent.w);

    TBN = mat3(
	    fragTangent,
	    fragBinormal,
	    fragNormal
    );

    vec4 lightClip = shadowLightVP[0] * worldPos;

    fragShadowDepth = lightClip.z / lightClip.w;
    fragShadowTexCoord = (lightClip.xy / lightClip.w) * 0.5 + 0.5;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
