#version 450
#extension GL_EXT_nonuniform_qualifier: require
#extension GL_EXT_shader_explicit_arithmetic_types_int64: require

layout (set = 0, binding = 0) uniform sampler2D textures[];

layout (push_constant) uniform PushConstant {
    uint offscreen;
} pc;

layout (location = 0) in vec2 in_uv;
layout (location = 0) out vec4 out_color;

vec3 aces_tonemap(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 reinhard_tonemap(vec3 color) {
    return color / (1.0 + color);
}

vec3 filmic_tonemap(vec3 x) {
    const float A = 0.22;
    const float B = 0.30;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.01;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}


void main() {
    float exposure = 0.6;
    vec3 hdr = texture(textures[pc.offscreen], in_uv).rgb;
    hdr *= exposure;
    vec3 mapped = aces_tonemap(hdr);
    out_color = vec4(mapped, 1.0);
}