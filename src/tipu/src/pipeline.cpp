#include "pipeline.h"

#include <volk.h>

#include "utils.h"

PipelineLayoutBuilder &PipelineLayoutBuilder::add_descriptor_set_layout(const VkDescriptorSetLayout layout) {
    set_layouts_.push_back(layout);
    return *this;
}

PipelineLayoutBuilder &PipelineLayoutBuilder::add_push_constant(const VkShaderStageFlags stage,
                                                                const uint32_t size,
                                                                const uint32_t offset) {
    const VkPushConstantRange range{stage, offset, size};
    push_constants_.push_back(range);
    return *this;
}

VkPipelineLayout PipelineLayoutBuilder::build(const Context *context, const std::string &debug_name) {
    const VkPipelineLayoutCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<uint32_t>(set_layouts_.size()),
        .pSetLayouts = set_layouts_.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(push_constants_.size()),
        .pPushConstantRanges = push_constants_.data()
    };

    VkPipelineLayout layout;
    check(vkCreatePipelineLayout(context->device_, &info, nullptr, &layout));
    std::printf("Created pipeline layout: %s.\n", debug_name.c_str());
    return layout;
}

PipelineBuilder::PipelineBuilder() {
    // default fall-back states
    vertex_input_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };

    input_assembly_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    viewport_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    rasterization_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };

    multisample_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE
    };

    depth_stencil_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE
    };

    blend_attachments_ = {
        {
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
        }
    };

    color_blend_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = blend_attachments_.data()
    };
}

PipelineBuilder &PipelineBuilder::add_shader(const VkShaderStageFlagBits stage,
                                             const VkShaderModule module,
                                             const char *entry) {
    const VkPipelineShaderStageCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = stage,
        .module = module,
        .pName = entry
    };
    shader_stages_.push_back(info);
    return *this;
}

PipelineBuilder &PipelineBuilder::set_vertex_layout(
    const VkVertexInputBindingDescription &binding,
    const std::vector<VkVertexInputAttributeDescription> &attributes) {
    vertex_input_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data()
    };
    return *this;
}

PipelineBuilder &PipelineBuilder::set_input_assembly(const VkPrimitiveTopology topology) {
    input_assembly_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology
    };
    return *this;
}

PipelineBuilder &PipelineBuilder::set_viewport(const uint32_t viewport_count,
                                               const uint32_t scissor_count,
                                               const bool dynamic) {
    viewport_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = viewport_count,
        .scissorCount = scissor_count,
    };

    if (dynamic) {
        dynamic_states_.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        dynamic_states_.push_back(VK_DYNAMIC_STATE_SCISSOR);
    }
    return *this;
}

PipelineBuilder &PipelineBuilder::set_rasterization(const VkCullModeFlags cull,
                                                    const VkFrontFace front,
                                                    const VkPolygonMode polygon_mode) {
    rasterization_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = polygon_mode,
        .cullMode = cull,
        .frontFace = front,
        .lineWidth = 1.0f
    };
    return *this;
}

PipelineBuilder &PipelineBuilder::set_multisampling(const VkSampleCountFlagBits samples) {
    multisample_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = samples
    };
    return *this;
}

PipelineBuilder &PipelineBuilder::set_depth_stencil(const bool depth_test,
                                                    const bool depth_write,
                                                    const VkCompareOp compare_op) {
    depth_stencil_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp = compare_op
    };
    return *this;
}

PipelineBuilder &PipelineBuilder::set_color_blend(const uint32_t attachment_count, const VkColorComponentFlags mask) {
    blend_attachments_.assign(attachment_count, {
                                  .blendEnable = VK_FALSE,
                                  .colorWriteMask = mask
                              });

    color_blend_state_ = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = attachment_count,
        .pAttachments = blend_attachments_.data()
    };
    return *this;
}

VkPipeline PipelineBuilder::build(const Context *context,
                                  const VkPipelineLayout layout,
                                  const std::vector<VkFormat> &color_formats,
                                  const VkFormat depth_format,
                                  const std::string &debug_name) {
    assert(color_formats.size() == blend_attachments_.size() && "Color format count must match blend attachment count");
    VkPipelineRenderingCreateInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = static_cast<uint32_t>(color_formats.size()),
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = depth_format
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamic_states_.size()),
        .pDynamicStates = dynamic_states_.data()
    };

    const VkGraphicsPipelineCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = static_cast<uint32_t>(shader_stages_.size()),
        .pStages = shader_stages_.data(),
        .pVertexInputState = &vertex_input_state_,
        .pInputAssemblyState = &input_assembly_state_,
        .pViewportState = &viewport_state_,
        .pRasterizationState = &rasterization_state_,
        .pMultisampleState = &multisample_state_,
        .pDepthStencilState = &depth_stencil_state_,
        .pColorBlendState = &color_blend_state_,
        .pDynamicState = &dynamic_state_info,
        .layout = layout
    };

    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(context->device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
    std::printf("Created pipeline: %s.\n", debug_name.c_str());
    return pipeline;
}
