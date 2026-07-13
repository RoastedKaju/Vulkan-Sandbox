#version 460
#extension GL_EXT_buffer_reference: require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_shader_explicit_arithmetic_types_int64: require

layout (push_constant) uniform PushConstants {
    mat4 model;
    uint64_t address;
    uint albedo;
    uint metallic;
    uint normal;
};

struct ShaderData {
    mat4 projection;
    mat4 view;
    vec3 camera;
    uint cubemap;
};

layout (buffer_reference, scalar) readonly buffer ShaderDataRef {
    ShaderData data;
};

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inTangent;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outWorldNormal;
layout (location = 2) out vec2 outUV;
layout (location = 3) out flat uint outAlbedoIndex;
layout (location = 4) out flat uint outMetallicIndex;
layout (location = 5) out flat uint outNormalIndex;
layout (location = 6) out flat uint outCubeIndex;
layout (location = 7) out vec3 outTangent;
layout (location = 8) out vec3 outBitangent;
layout (location = 9) out vec3 outNormal;
layout (location = 10) out vec3 outCameraPos;

void main() {
    ShaderData sceneData = ShaderDataRef(address).data;

    // World-space position
    vec4 worldPos = model * vec4(inPosition, 1.0);

    // Normal calculation
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    vec3 B = normalize(cross(N, T) * inTangent.w);

    outTangent = T;
    outBitangent = B;
    outNormal = N;

    // Outputs
    outWorldPos = worldPos.xyz;
    outWorldNormal = normalize(normalMatrix * inNormal);
    outUV = inUV;

    outAlbedoIndex = albedo;
    outMetallicIndex = metallic;
    outNormalIndex = normal;
    outCubeIndex = sceneData.cubemap;
    outCameraPos = sceneData.camera;

    // Final clip-space position
    gl_Position = sceneData.projection * sceneData.view * worldPos;
}