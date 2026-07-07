#version 460
#extension GL_EXT_nonuniform_qualifier: require

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in flat uint inTexIndex;
layout (location = 3) in vec3 inPosition;
layout (location = 4) in flat uint inCubemapIndex;
layout (location = 5) in vec3 inCameraPos;

layout (set = 0, binding = 0) uniform sampler2D bindless_textures[];
layout (set = 0, binding = 1) uniform samplerCube bindless_cubemaps[];

layout (location = 0) out vec4 outColor;

void main() {
    vec3 I = normalize(inPosition - inCameraPos);
    vec3 N = normalize(inNormal);
    vec3 R = reflect(I, N);

    outColor = vec4(texture(bindless_cubemaps[nonuniformEXT(inCubemapIndex)], R).rgb, 1.0);
}