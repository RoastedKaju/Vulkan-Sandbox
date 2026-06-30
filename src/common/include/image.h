#pragma once

#include <vulkan/vulkan.h>

struct ImageState {
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkAccessFlags2 access{0};
    VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
};

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    ImageState state{};
};
