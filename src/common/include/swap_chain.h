#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

class Context;

class SwapChain {
public:
    SwapChain() = default;

    void init_swap_chain(const Context *context, VkFormat image_format = VK_FORMAT_B8G8R8A8_SRGB);

    void setup_depth_attachment(const Context *context);

    std::vector<VkImage> get_images() { return swap_chain_images_; };

private:
    bool is_swap_chain_dirty_{false};
    VkSwapchainKHR swap_chain_{VK_NULL_HANDLE};
    VkFormat swap_chain_format_{VK_FORMAT_R8G8B8A8_SRGB};
    std::vector<VkImage> swap_chain_images_;
    std::vector<VkImageView> swap_chain_image_views_;

    // depth attachment
    VkImage depth_image_{VK_NULL_HANDLE};
    VkImageView depth_image_view_{VK_NULL_HANDLE};
    VmaAllocation depth_image_allocation_{VK_NULL_HANDLE};
};
