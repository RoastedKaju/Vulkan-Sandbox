#version 460
#extension GL_EXT_nonuniform_qualifier: require

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in flat uint inTexIndex;

layout (set = 0, binding = 0) uniform sampler2D bindless_textures[];

layout (location = 0) out vec4 outColor;

void main() {
    vec4 tex_color = texture(bindless_textures[nonuniformEXT(inTexIndex)], inUV);
    outColor = tex_color;
}