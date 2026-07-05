#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "context.h"

class Context;


/**
 * A pipeline layout is the interface contract between shaders and the application.
 * It defines: descriptor sets, push constants.
 * You can have multiple pipelines but one layout, layouts are cheap.
 * You can also swap pipelines without rebinding descriptors.
 * Layout = "Input Contract" while Pipeline = "Execution recipe"
 */
class PipelineLayoutBuilder {
public:
    PipelineLayoutBuilder() = default;

    PipelineLayoutBuilder(const PipelineLayoutBuilder &) = delete;

    PipelineLayoutBuilder &operator=(const PipelineLayoutBuilder &) = delete;

    PipelineLayoutBuilder(PipelineLayoutBuilder &&) = delete;

    PipelineLayoutBuilder &operator=(PipelineLayoutBuilder &&) = delete;

    PipelineLayoutBuilder &add_descriptor_set_layout(VkDescriptorSetLayout layout);

    PipelineLayoutBuilder &add_push_constant(VkShaderStageFlags stage, uint32_t size, uint32_t offset = 0);

    [[nodiscard]] VkPipelineLayout build(const Context *context, const std::string &debug_name = "none");

private:
    std::vector<VkDescriptorSetLayout> set_layouts_;
    std::vector<VkPushConstantRange> push_constants_;
};

class PipelineBuilder {
public:
    PipelineBuilder();

    PipelineBuilder(const PipelineBuilder &) = delete;

    PipelineBuilder &operator=(const PipelineBuilder &) = delete;

    PipelineBuilder(PipelineBuilder &&) = delete;

    PipelineBuilder &operator=(PipelineBuilder &&) = delete;

    PipelineBuilder &add_shader(VkShaderStageFlagBits stage, VkShaderModule module, const char *entry = "main");

    PipelineBuilder &set_vertex_layout(const VkVertexInputBindingDescription &binding,
                                       const std::vector<VkVertexInputAttributeDescription> &attributes);

    PipelineBuilder &set_input_assembly(VkPrimitiveTopology topology);

    PipelineBuilder &set_viewport(uint32_t viewport_count, uint32_t scissor_count, bool dynamic = true);

    PipelineBuilder &set_rasterization(VkCullModeFlags cull,
                                       VkFrontFace front,
                                       VkPolygonMode polygon_mode = VK_POLYGON_MODE_FILL);

    PipelineBuilder &set_multisampling(VkSampleCountFlagBits samples);

    PipelineBuilder &set_depth_stencil(bool depth_test, bool depth_write, VkCompareOp compare_op);

    PipelineBuilder &set_color_blend(uint32_t attachment_count, const VkColorComponentFlags mask);

    [[nodiscard]] VkPipeline build(const Context *context,
                                   VkPipelineLayout layout,
                                   const std::vector<VkFormat> &color_formats,
                                   VkFormat depth_format,
                                   const std::string &debug_name = "none");

private:
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages_;
    std::vector<VkDynamicState> dynamic_states_;
    VkPipelineVertexInputStateCreateInfo vertex_input_state_{};
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_{};
    VkPipelineViewportStateCreateInfo viewport_state_{};
    VkPipelineRasterizationStateCreateInfo rasterization_state_{};
    VkPipelineMultisampleStateCreateInfo multisample_state_{};
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state_{};
    std::vector<VkPipelineColorBlendAttachmentState> blend_attachments_;
    VkPipelineColorBlendStateCreateInfo color_blend_state_{};
};
