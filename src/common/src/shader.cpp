#include "shader.h"

#include <volk.h>

#include "utils.h"
#include "context.h"

VkShaderModule Shader::create_shader_module(const Context *context,
                                            const std::filesystem::path &path,
                                            const shaderc_shader_kind kind) {
    // raw shader code
    const std::string shader_code = read_text_file(path);

    // compile shader code into SPIR-V
    const shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

    // ReSharper disable once CppTooWideScopeInitStatement
    const auto compile_result{compiler.CompileGlslToSpv(shader_code, kind, path.string().c_str(), options)};

    if (compile_result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error(compile_result.GetErrorMessage());
    }

    std::vector<uint32_t> shader_binary{compile_result.begin(), compile_result.end()};

    // create shader module
    const VkShaderModuleCreateInfo shader_module_create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_binary.size() * sizeof(uint32_t),
        .pCode = shader_binary.data()
    };

    VkShaderModule shader_module{VK_NULL_HANDLE};

    // ReSharper disable once CppTooWideScopeInitStatement
    const auto result = vkCreateShaderModule(context->device_, &shader_module_create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module.");
    }

    std::printf("Shader module created: %s.\n", path.string().c_str());
    return shader_module;
}
