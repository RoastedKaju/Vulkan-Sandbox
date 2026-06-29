#pragma once

#include <filesystem>

#include <vulkan/vulkan.h>
#include <shaderc/shaderc.hpp>

class Context;

class Shader {
public:
    static VkShaderModule create_shader_module(const Context *context,
                                               const std::filesystem::path &path,
                                               const shaderc_shader_kind kind);

private:
    Shader() = default;

    ~Shader() = default;
};
