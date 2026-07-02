#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

layout(push_constant) uniform PushConstants {
    uint64_t address;
};

struct ShaderData {
    mat4 projection;
    mat4 view;
    mat4 model;
    uint tex_index;
};

layout(buffer_reference, scalar) readonly buffer ShaderDataRef {
    ShaderData data;
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out flat uint outTexIndex;

void main() {
    ShaderData sd = ShaderDataRef(address).data;

    gl_Position = sd.projection * sd.view * sd.model * vec4(inPosition, 1.0);
    outNormal = mat3(sd.view * sd.model) * inNormal;
    outUV = inUV;
    outTexIndex = sd.tex_index;
}