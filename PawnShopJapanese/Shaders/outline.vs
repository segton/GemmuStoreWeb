#version 330

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexTangent;

uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

uniform float outlineWidth;

void main()
{
    vec3 smoothNormal = vertexTangent.xyz;

    if (length(smoothNormal) < 0.0001)
    {
        smoothNormal = vertexNormal;
    }

    mat4 modelView = matView * matModel;

    vec4 viewPosition = modelView * vec4(vertexPosition, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(modelView)));
    vec3 viewNormal = normalize(normalMatrix * smoothNormal);

    viewPosition.xyz += viewNormal * (-viewPosition.z) * outlineWidth / 1000.0;

    gl_Position = matProjection * viewPosition;
}