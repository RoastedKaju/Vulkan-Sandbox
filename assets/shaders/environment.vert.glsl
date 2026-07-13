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

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out flat uint outTexIndex;
layout (location = 3) out vec3 outPosition;
layout (location = 4) out flat uint outCubemapIndex;
layout (location = 5) out vec3 outCameraPos;

void main() {
    ShaderData sceneData = ShaderDataRef(address).data;

    outNormal = mat3(transpose(inverse(model))) * inNormal;
    outPosition = vec3(model * vec4(inPosition, 1.0));
    outUV = inUV;
    outTexIndex = albedo;
    outCubemapIndex = sceneData.cubemap;
    outCameraPos = sceneData.camera;

    gl_Position = sceneData.projection * sceneData.view * model * vec4(inPosition, 1.0);
}