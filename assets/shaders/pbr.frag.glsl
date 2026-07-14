#version 460
#extension GL_EXT_nonuniform_qualifier: require

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inWorldNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in flat uint inAlbedoIndex;
layout (location = 4) in flat uint inMetallicIndex;
layout (location = 5) in flat uint inNormalIndex;
layout (location = 6) in flat uint inCubeIndex;
layout (location = 7) in vec3 inTangent;
layout (location = 8) in vec3 inBitangent;
layout (location = 9) in vec3 inNormal;
layout (location = 10) in vec3 inCameraPos;

layout (set = 0, binding = 0) uniform sampler2D bindless_textures[];

layout (location = 0) out vec4 outColor;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;

    return a2 / (3.14159265 * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float ggx1 = GeometrySchlickGGX(max(dot(N, V), 0.0), roughness);
    float ggx2 = GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);

    return ggx1 * ggx2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) *
    pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec3 albedo = texture(bindless_textures[nonuniformEXT(inAlbedoIndex)], inUV).rgb;

    // Metallic-Roughness texture
    vec3 mr = texture(bindless_textures[nonuniformEXT(inMetallicIndex)], inUV).rgb;

    float roughness = mr.g;
    float metallic = mr.b;

    // Normal mapping
    mat3 TBN = mat3(
    normalize(inTangent),
    normalize(inBitangent),
    normalize(inNormal)
    );

    vec3 tangentNormal = texture(bindless_textures[nonuniformEXT(inNormalIndex)], inUV).xyz;

    tangentNormal = tangentNormal * 2.0 - 1.0;

    vec3 N = normalize(TBN * tangentNormal);

    // Camera & lighting
    vec3 V = normalize(inCameraPos - inWorldPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));
    vec3 H = normalize(V + L);

    // Cook-Torrance BRDF
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;

    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;

    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);

    vec3 radiance = vec3(12.0);

    vec3 Lo = (kD * albedo / 3.14159265 + specular) * radiance * NdotL;

    // Simple ambient term
    vec3 ambient = vec3(0.0065) * albedo;

    vec3 color = ambient + Lo;

    outColor = vec4(color, 1.0);
}