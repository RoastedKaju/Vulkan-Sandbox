#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "image.h"

class Context;

class SwapChain {
public:
    SwapChain() = default;

    SwapChain(const SwapChain &) = delete;

    SwapChain(SwapChain &&) = delete;

    SwapChain &operator=(const SwapChain &) = delete;

    SwapChain &operator=(SwapChain &&) = delete;

    ~SwapChain() = default;

    void init_swap_chain(const Context *context, VkFormat image_format = VK_FORMAT_R8G8B8A8_SRGB);

    void setup_depth_attachment(const Context *context);

    VkSwapchainKHR get() const { return swap_chain_; }
    VkFormat get_format() const { return swap_chain_format_; };
    std::vector<VkImage> get_images() { return swap_chain_images_; };
    std::vector<VkImageView> get_views() { return swap_chain_image_views_; };
    std::vector<ImageState> &get_states() { return swap_chain_image_states_; }
    VkFormat get_depth_format() const { return depth_format_; };
    VkImage get_depth_image() const { return depth_image_; };
    VkImageView get_depth_image_view() const { return depth_image_view_; };
    ImageState &get_depth_state() { return depth_image_state_; }
    void mark_swap_chain_dirty() { is_swap_chain_dirty_ = true; }
    bool is_swap_chain_dirty() const { return is_swap_chain_dirty_; }

private:
    bool is_swap_chain_dirty_{false};
    VkSwapchainKHR swap_chain_{VK_NULL_HANDLE};
    VkFormat swap_chain_format_{VK_FORMAT_R8G8B8A8_SRGB};
    std::vector<VkImage> swap_chain_images_;
    std::vector<VkImageView> swap_chain_image_views_;
    std::vector<ImageState> swap_chain_image_states_;

    // depth attachment
    VkImage depth_image_{VK_NULL_HANDLE};
    VkImageView depth_image_view_{VK_NULL_HANDLE};
    ImageState depth_image_state_;
    VkFormat depth_format_{VK_FORMAT_D32_SFLOAT};
    VmaAllocation depth_image_allocation_{VK_NULL_HANDLE};
};
