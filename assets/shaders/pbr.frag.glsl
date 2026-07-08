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

layout (set = 0, binding = 0) uniform sampler2D bindless_textures[];

layout (location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(bindless_textures[nonuniformEXT(inAlbedoIndex)], inUV).rgb;

    // Normal
    mat3 TBN = mat3(
    normalize(inTangent),
    normalize(inBitangent),
    normalize(inNormal)
    );

    vec3 tangentNormal = texture(bindless_textures[nonuniformEXT(inNormalIndex)], inUV).xyz;

    tangentNormal = tangentNormal * 2.0 - 1.0;

    vec3 N = normalize(TBN * tangentNormal);

    // Simple directional light (world space)
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));

    // Lambert diffuse
    float NdotL = max(dot(N, L), 0.0);

    // Small ambient term so the unlit side isn't completely black
    vec3 ambient = 0.05 * albedo;

    vec3 color = ambient + albedo * NdotL;

    outColor = vec4(color, 1.0);
}