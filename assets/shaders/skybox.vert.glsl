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
    uint textureIndex;
};

layout (buffer_reference, scalar) readonly buffer ShaderDataRef {
    ShaderData data;
};

const vec3 pos[8] = vec3[8](
vec3(-1.0, -1.0, 1.0),
vec3(1.0, -1.0, 1.0),
vec3(1.0, 1.0, 1.0),
vec3(-1.0, 1.0, 1.0),

vec3(-1.0, -1.0, -1.0),
vec3(1.0, -1.0, -1.0),
vec3(1.0, 1.0, -1.0),
vec3(-1.0, 1.0, -1.0)
);

const int indices[36] = int[36](
0, 1, 2, 2, 3, 0,
1, 5, 6, 6, 2, 1,
7, 6, 5, 5, 4, 7,
4, 0, 3, 3, 7, 4,
4, 5, 1, 1, 0, 4,
3, 2, 6, 6, 7, 3
);

layout (location = 0) out vec3 outDirection;
layout (location = 1) out flat uint outTexIndex;

void main()
{
    ShaderData sceneData = ShaderDataRef(address).data;

    int idx = indices[gl_VertexIndex];
    gl_Position = sceneData.projection * mat4(mat3(sceneData.view)) * vec4(1.0 * pos[idx], 1.0);
    outDirection = pos[idx].xyz;
    outTexIndex = sceneData.textureIndex;
}