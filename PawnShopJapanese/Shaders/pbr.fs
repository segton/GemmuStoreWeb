#version 120

#define MAX_LIGHTS              4
#define LIGHT_DIRECTIONAL       0
#define LIGHT_POINT             1
#define PI 3.14159265358979323846

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
    float intensity;
};

// Input vertex attributes (from vertex shader)
varying vec3 fragPosition;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;
varying vec4 shadowPos;
varying mat3 TBN;

// Input uniform values
uniform int numOfLights;
uniform sampler2D albedoMap;
uniform sampler2D mraMap;
uniform sampler2D normalMap;
uniform sampler2D emissiveMap; // r: Hight g:emissive

uniform vec2 tiling;
uniform vec2 offset;

uniform int useTexAlbedo;
uniform int useTexNormal;
uniform int useTexMRA;
uniform int useTexEmissive;

uniform vec4  albedoColor;
uniform vec4  emissiveColor;
uniform float normalValue;
uniform float metallicValue;
uniform float roughnessValue;
uniform float aoValue;
uniform float emissivePower;

// Input lighting values
uniform Light lights[MAX_LIGHTS];
uniform vec3 viewPos;

uniform vec3 ambientColor;
uniform float ambient;


uniform int useManualFloorTBN;
// Reflectivity in range 0.0 to 1.0
// NOTE: Reflectivity is increased when surface view at larger angle
vec3 GetNormalFromMap(vec2 uv)
{
    vec3 N = normalize(fragNormal);

    if (useTexNormal == 0) 
        return N;

    vec3 tangentNormal = texture2D(normalMap, uv).rgb * 2.0 - 1.0;
    tangentNormal = normalize(vec3(tangentNormal.xy * normalValue, tangentNormal.z));

    vec3 Q1  = dFdx(fragPosition);
    vec3 Q2  = dFdy(fragPosition);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    // Do NOT divide by det here
    vec3 T = normalize(Q1 * st2.y - Q2 * st1.y);
    T = normalize(T - N * dot(N, T));

    vec3 B = -normalize(cross(N, T));   // if this looks inverted, remove the minus

    mat3 tbn = mat3(T, B, N);
    return normalize(tbn * tangentNormal);
}
vec3 SchlickFresnel(float hDotV,vec3 refl)
{
    return refl + (1.0 - refl)*pow(1.0 - hDotV, 5.0);
}


float GgxDistribution(float nDotH,float roughness)
{
    float a = roughness*roughness*roughness*roughness;
    float d = nDotH*nDotH*(a - 1.0) + 1.0;
    d = PI*d*d;
    return (a/max(d,0.0000001));
}

float GeomSmith(float nDotV,float nDotL,float roughness)
{
    float r = roughness + 1.0;
    float k = r*r/8.0;
    float ik = 1.0 - k;
    float ggx1 = nDotV/(nDotV*ik + k);
    float ggx2 = nDotL/(nDotL*ik + k);
    return ggx1*ggx2;
}

vec3 ComputePBR()
{
    vec2 uv = fragTexCoord * tiling + offset;

    vec3 albedo = albedoColor.rgb;
    if (useTexAlbedo == 1)
    {
        albedo *= texture2D(albedoMap, uv).rgb;
    }

    float metallic = clamp(metallicValue, 0.0, 1.0);
    float roughness = clamp(roughnessValue, 0.0, 1.0);
    float ao = clamp(aoValue, 0.0, 1.0);

    if (useTexMRA == 1)
    {
        vec4 mra = texture2D(mraMap, uv);
        metallic = clamp(mra.r + metallicValue, 0.04, 1.0);
        roughness = clamp(mra.g + roughnessValue, 0.04, 1.0);
        ao = (mra.b + aoValue) * 0.5;
    }

    vec3 N = GetNormalFromMap(uv);

    vec3 V = normalize(viewPos - fragPosition);

    vec3 emissive = vec3(0.0);
    if (useTexEmissive == 1)
    {
        emissive = texture2D(emissiveMap, uv).ggg * emissiveColor.rgb * emissivePower;
    }

    vec3 baseRefl = mix(vec3(0.04), albedo, metallic);
    vec3 lightAccum = vec3(0.0);

    for (int i = 0; i < 4; i++)
    {
        vec3 L = normalize(lights[i].position - fragPosition);
        vec3 H = normalize(V + L);
        float dist = length(lights[i].position - fragPosition);
        float attenuation = 1.0 / (dist * dist * 0.23);
        vec3 radiance = lights[i].color.rgb * lights[i].intensity * attenuation;

        float nDotV = max(dot(N, V), 0.0000001);
        float nDotL = max(dot(N, L), 0.0000001);
        float hDotV = max(dot(H, V), 0.0);
        float nDotH = max(dot(N, H), 0.0);

        float D = GgxDistribution(nDotH, roughness);
        float G = GeomSmith(nDotV, nDotL, roughness);
        vec3 F = SchlickFresnel(hDotV, baseRefl);

        vec3 spec = (D * G * F) / (4.0 * nDotV * nDotL);

        vec3 kD = vec3(1.0) - F;
        kD *= 1.0 - metallic;

        lightAccum += ((kD * albedo / PI + spec) * radiance * nDotL) * float(lights[i].enabled);
    }

    vec3 ambientFinal = (ambientColor + albedo) * ambient * 0.5;
    return ambientFinal + lightAccum * ao + emissive;
}
void main()
{
    vec3 color = ComputePBR();

    // HDR tonemapping
    color = pow(color, color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    gl_FragColor = vec4(color,1.0);
}