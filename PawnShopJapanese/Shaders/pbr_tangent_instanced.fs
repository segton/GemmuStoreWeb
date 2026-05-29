#version 330

#define MAX_LIGHTS 12
#define LIGHT_DIRECTIONAL       0
#define LIGHT_POINT             1
#define PI 3.14159265358979323846
#define MAX_SHADOW_CASTERS 2

struct Light {
    int enabled;
    int type;
    vec3 position;
    vec3 target;
    vec4 color;
    float intensity;
    float range;
};

// Input vertex attributes from vertex shader
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
in mat3 TBN;

in vec2 fragShadowTexCoord;
in float fragShadowDepth;

// Output fragment color
out vec4 finalColor;

// Input uniform values
uniform int numOfLights;

uniform sampler2D albedoMap;
uniform sampler2D mraMap;
uniform sampler2D normalMap;
uniform sampler2D emissiveMap;

uniform int receiveShadows;

uniform int alphaMode;       // 0 opaque, 1 mask, 2 blend
uniform float alphaCutoff;

uniform vec2 tiling;
uniform vec2 offset;

uniform int useTexAlbedo;
uniform int useTexNormal;
uniform int useTexMRA;
uniform int useTexEmissive;

// 1 = glTF metallicRoughness texture: G roughness, B metallic.
// 0 = custom MRA texture: R metallic, G roughness, B AO.
uniform int useGltfMetallicRoughness;

uniform vec4  albedoColor;
uniform vec4  emissiveColor;
uniform float normalValue;
uniform float metallicValue;
uniform float roughnessValue;
uniform float aoValue;
uniform float emissivePower;

uniform samplerCube environmentMap;
uniform float reflectionStrength;

// Input lighting values
uniform Light lights[MAX_LIGHTS];
uniform vec3 viewPos;

uniform vec3 ambientColor;
uniform float ambient;

uniform int shadowCasterCount;

uniform sampler2D shadowMap0;
uniform sampler2D shadowMap1;

uniform int shadowLightIndex[MAX_SHADOW_CASTERS];
uniform float shadowBias[MAX_SHADOW_CASTERS];
uniform float shadowStrength[MAX_SHADOW_CASTERS];

vec3 SchlickFresnel(float hDotV, vec3 refl)
{
    return refl + (1.0 - refl) * pow(1.0 - hDotV, 5.0);
}

float GgxDistribution(float nDotH, float roughness)
{
    float a = roughness * roughness * roughness * roughness;
    float d = nDotH * nDotH * (a - 1.0) + 1.0;
    d = PI * d * d;
    return a / max(d, 0.0000001);
}

float GeomSmith(float nDotV, float nDotL, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    float ik = 1.0 - k;

    float ggx1 = nDotV / (nDotV * ik + k);
    float ggx2 = nDotL / (nDotL * ik + k);

    return ggx1 * ggx2;
}

float SampleShadowMap(int index, vec2 uv)
{
    if (index == 0) return texture(shadowMap0, uv).r;
    if (index == 1) return texture(shadowMap1, uv).r;
    return 1.0;
}

float CalculateMainShadow(vec3 N, vec3 L)
{
    if (fragShadowTexCoord.x < 0.0 || fragShadowTexCoord.x > 1.0 ||
        fragShadowTexCoord.y < 0.0 || fragShadowTexCoord.y > 1.0)
    {
        return 1.0;
    }

    float currentDepth = fragShadowDepth * 0.5 + 0.5;

    if (currentDepth < 0.0 || currentDepth > 1.0)
    {
        return 1.0;
    }

    float ndotl = max(dot(N, L), 0.0);

    float bias = max(
        shadowBias[0] * (1.0 - ndotl),
        shadowBias[0] * 0.35
    );

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap0, 0));

    float radius = 2.5;
    float transition = 0.003;

    float lit = 0.0;
    float weightSum = 0.0;

    for (int x = -3; x <= 3; x++)
    {
        for (int y = -3; y <= 3; y++)
        {
            vec2 o = vec2(x, y);
            float weight = 1.0 / (1.0 + dot(o, o) * 0.35);

            vec2 sampleUv = fragShadowTexCoord + o * texelSize * radius;

            if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
                sampleUv.y < 0.0 || sampleUv.y > 1.0)
            {
                lit += weight;
                weightSum += weight;
                continue;
            }

            float closestDepth = texture(shadowMap0, sampleUv).r;

            if (closestDepth >= 0.999)
            {
                lit += weight;
                weightSum += weight;
                continue;
            }

            float depthDiff = closestDepth - (currentDepth - bias);

            float sampleLit = smoothstep(
                -transition,
                 transition,
                 depthDiff
            );

            lit += sampleLit * weight;
            weightSum += weight;
        }
    }

    float visibility = lit / max(weightSum, 0.0001);

    float strength = clamp(shadowStrength[0], 0.0, 1.0);

    // Darkest possible shadow floor.
    // Lower = darker shadows, but never mathematically negative.
    float darkestShadow = 0.02;

    float shadow = mix(darkestShadow, 1.0, visibility);
    shadow = mix(1.0, shadow, strength);

    return clamp(shadow, 0.0, 1.0);
}

vec3 ComputePBR()
{
    vec2 uv = vec2(
        fragTexCoord.x * tiling.x + offset.x,
        fragTexCoord.y * tiling.y + offset.y
    );

    vec3 albedo = albedoColor.rgb;

    if (useTexAlbedo == 1)
    {
        albedo *= texture(albedoMap, uv).rgb;
    }

    float metallic = clamp(metallicValue, 0.0, 1.0);
    float roughness = clamp(roughnessValue, 0.04, 1.0);
    float ao = clamp(aoValue, 0.0, 1.0);

    if (useTexMRA == 1)
    {
        vec3 mra = texture(mraMap, uv).rgb;

        if (useGltfMetallicRoughness == 1)
        {
            // glTF metallicRoughness:
            // G = roughness, B = metallic. AO is usually separate.
            roughness = clamp(mra.g * roughnessValue, 0.04, 1.0);
            metallic = clamp(mra.b * metallicValue, 0.0, 1.0);
            ao = clamp(aoValue, 0.0, 1.0);
        }
        else
        {
            // Custom packed MRA:
            // R = metallic, G = roughness, B = AO.
            metallic = clamp(mra.r * metallicValue, 0.0, 1.0);
            roughness = clamp(mra.g * roughnessValue, 0.04, 1.0);
            ao = clamp(mra.b * aoValue, 0.0, 1.0);
        }
    }

    vec3 N = normalize(fragNormal);

    if (useTexNormal == 1)
    {
        vec3 tangentNormal = texture(normalMap, uv).rgb;
        tangentNormal = normalize(tangentNormal * 2.0 - 1.0);
        tangentNormal.xy *= normalValue;
        tangentNormal = normalize(tangentNormal);

        N = normalize(TBN * tangentNormal);
    }

    vec3 V = normalize(viewPos - fragPosition);

    vec3 R = reflect(-V, N);
    vec3 envReflection = texture(environmentMap, R).rgb;

    vec3 emissive = vec3(0.0);

    if (useTexEmissive == 1)
    {
        emissive =
            texture(emissiveMap, uv).rgb *
            emissiveColor.rgb *
            emissivePower;
    }

    vec3 baseRefl = mix(vec3(0.04), albedo.rgb, metallic);
    vec3 lightAccum = vec3(0.0);

    // Calculate the main shadow once, so fill lights can respect it too.
    float mainShadowFactor = 1.0;

    if (receiveShadows == 1 &&
        shadowCasterCount > 0)
    {
        int shadowIndex = shadowLightIndex[0];

        if (shadowIndex >= 0 && shadowIndex < numOfLights)
        {
            vec3 shadowL;

            if (lights[shadowIndex].type == LIGHT_DIRECTIONAL)
            {
                shadowL = normalize(lights[shadowIndex].position - lights[shadowIndex].target);
            }
            else
            {
                shadowL = normalize(lights[shadowIndex].position - fragPosition);
            }

            mainShadowFactor = CalculateMainShadow(N, shadowL);
            mainShadowFactor = clamp(mainShadowFactor, 0.0, 1.0);
        }
    }

    for (int i = 0; i < numOfLights; i++)
    {
        if (lights[i].enabled == 0)
            continue;

        vec3 L;
        float attenuation = 1.0;

        if (lights[i].type == LIGHT_DIRECTIONAL)
        {
            L = normalize(lights[i].position - lights[i].target);
        }
        else
        {
            vec3 toLight = lights[i].position - fragPosition;
            float dist = length(toLight);

            float range = max(lights[i].range, 0.001);

            if (dist > range)
                continue;

            L = toLight / max(dist, 0.0001);

            float d = clamp(dist / range, 0.0, 1.0);

            // Smooth falloff to zero at range limit.
            float rangeAtten = 1.0 - d * d;
            rangeAtten *= rangeAtten;

            attenuation = rangeAtten / (1.0 + dist * dist * 0.35);
        }

        vec3 H = normalize(V + L);

        vec3 radiance =
            lights[i].color.rgb *
            lights[i].intensity *
            attenuation;

        float shadowFactor = 1.0;

        if (receiveShadows == 1 &&
            shadowCasterCount > 0 &&
            shadowLightIndex[0] == i)
        {
            shadowFactor = mainShadowFactor;
        }

        shadowFactor = clamp(shadowFactor, 0.0, 1.0);

        // Fill lights still work, but do not completely erase main shadows.
        if (receiveShadows == 1 &&
            shadowCasterCount > 0 &&
            shadowLightIndex[0] != i)
        {
            float fillLightShadowMin = 0.20;
            float fillShadowMask = mix(fillLightShadowMin, 1.0, mainShadowFactor);
            radiance *= fillShadowMask;
        }

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

        lightAccum += ((kD * albedo.rgb / PI + spec) * radiance * nDotL)
                      * shadowFactor;
    }

    // Preserve material color instead of washing it toward white.
    vec3 ambientFinal = albedo * ambientColor * ambient;

    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    float reflectionAmount =
        reflectionStrength *
        (1.0 - roughness) *
        (0.04 + 0.96 * fresnel);

    return (
        ambientFinal +
        lightAccum * ao +
        emissive +
        envReflection * reflectionAmount
    );
}

void main()
{
    vec3 color = ComputePBR();

    // Reinhard tonemapping + gamma.
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    vec2 uv = vec2(
        fragTexCoord.x * tiling.x + offset.x,
        fragTexCoord.y * tiling.y + offset.y
    );

    float alpha = albedoColor.a;

    if (useTexAlbedo == 1)
    {
        alpha *= texture(albedoMap, uv).a;
    }

    if (alphaMode == 0)
    {
        finalColor = vec4(color, 1.0);
    }
    else if (alphaMode == 1)
    {
        if (alpha < alphaCutoff)
        {
            discard;
        }

        finalColor = vec4(color, 1.0);
    }
    else
    {
        finalColor = vec4(color, alpha);
    }
}
