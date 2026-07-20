#version 460
#extension GL_EXT_buffer_reference: require
#extension GL_EXT_scalar_block_layout: require
#extension GL_EXT_shader_explicit_arithmetic_types_int64: require

layout (push_constant) uniform PushConstants {
    mat4 model;
    uint64_t address;
    uint64_t bone_address;
    uint albedo;
};

struct ShaderData {
    mat4 projection;
    mat4 view;
};

layout (buffer_reference, scalar) readonly buffer ShaderDataRef {
    ShaderData data;
};

layout (buffer_reference, scalar) readonly buffer BoneMatricesRef {
    mat4 matrices[];
};

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inTangent;
layout (location = 4) in ivec4 inBoneIds;
layout (location = 5) in vec4 inBoneWeights;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out flat uint outTexIndex;

void main() {
    ShaderData shaderData = ShaderDataRef(address).data;

    mat4 skinMatrix = mat4(0.0);
    float weightSum = 0.0;

    if (bone_address != uint64_t(0)) {
        BoneMatricesRef bones = BoneMatricesRef(bone_address);
        for (int i = 0; i < 4; ++i) {
            if (inBoneIds[i] >= 0) {
                skinMatrix += inBoneWeights[i] * bones.matrices[inBoneIds[i]];
                weightSum += inBoneWeights[i];
            }
        }
    }
    if (weightSum < 0.001) {
        skinMatrix = mat4(1.0);
    }

    vec4 skinnedPosition = skinMatrix * vec4(inPosition, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;

    gl_Position = shaderData.projection * shaderData.view * model * skinnedPosition;
    outNormal = mat3(shaderData.view * model) * skinnedNormal;
    outUV = inUV;
    outTexIndex = albedo;
}