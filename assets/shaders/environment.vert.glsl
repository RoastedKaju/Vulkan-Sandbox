#version 460
#extension GL_EXT_buffer_reference: require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_shader_explicit_arithmetic_types_int64: require

layout (push_constant) uniform PushConstants {
    uint64_t address;
};

struct ShaderData {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 camera;
    uint tex_index;
    uint cubemap_index;
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

    outNormal = mat3(transpose(inverse(sceneData.model))) * inNormal;
    outPosition = vec3(sceneData.model * vec4(inPosition, 1.0));
    outUV = inUV;
    outTexIndex = sceneData.tex_index;
    outCubemapIndex = sceneData.cubemap_index;
    outCameraPos = sceneData.camera;

    gl_Position = sceneData.projection * sceneData.view * sceneData.model * vec4(inPosition, 1.0);
}