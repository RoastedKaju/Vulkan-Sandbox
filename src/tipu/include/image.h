#pragma once

#include <limits>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

/**
 * Image state is used by memory barriers to save the current state of an image.
 */
struct ImageState {
    VkImageLayout layout_{VK_IMAGE_LAYOUT_UNDEFINED};
    VkAccessFlags2 access_{0};
    VkPipelineStageFlags2 stage_{VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
};


/**
 * This is a GPU resource, it is a container for vulkan image handle
 */
struct Image {
    VkImage image_{VK_NULL_HANDLE};
    VkImageView view_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};

    VkFormat format_{VK_FORMAT_UNDEFINED};
    VkImageAspectFlags aspect_{VK_IMAGE_ASPECT_COLOR_BIT};
    VkImageType type_{VK_IMAGE_TYPE_2D};
    VkImageUsageFlags usage_{0};

    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t depth_{1};
    uint32_t mip_levels_{1};
    uint32_t array_layers_{1};
    VkSampleCountFlagBits samples_{VK_SAMPLE_COUNT_1_BIT};

    uint32_t bindless_index_{std::numeric_limits<uint32_t>::max()};

    ImageState state_{};

    [[nodiscard]] VkDescriptorImageInfo descriptor_info(const VkSampler sampler) const {
        return VkDescriptorImageInfo{
            .sampler = sampler,
            .imageView = view_,
            .imageLayout = state_.layout_
        };
    }
};
