#version 460
#extension GL_EXT_nonuniform_qualifier: require

layout (location = 0) in vec3 inDirection;
layout (location = 1) in flat uint inTexIndex;

layout (set = 0, binding = 0) uniform sampler2D bindless_textures[];
layout (set = 0, binding = 1) uniform samplerCube bindless_cubemaps[];

layout (location = 0) out vec4 outColor;

void main()
{
    vec4 tex_color = texture(bindless_cubemaps[nonuniformEXT(inTexIndex)], inDirection);
    outColor = tex_color;
}