#pragma once

#include <array>

#include <vulkan/vulkan.h>

#include "image.h"

struct Attachment {
    static inline constexpr uint32_t kMaxColorAttachments = 8;

    Attachment() = default;

    uint32_t add_color(VkImageLayout layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                       VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                       VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE,
                       VkClearColorValue clear = {0.0f, 0.0f, 0.0f, 1.0f},
                       VkResolveModeFlagBits resolve_mode = VK_RESOLVE_MODE_NONE,
                       VkImageLayout resolve_layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

    void set_depth(VkImageLayout layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                   VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                   VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                   VkClearDepthStencilValue clear = {1.0f, 0});

    uint32_t color_count() const { return color_count_; }
    bool has_depth() const { return has_depth_; }

    const VkRenderingAttachmentInfo &color(const uint32_t index) const { return colors_[index]; }
    const VkRenderingAttachmentInfo &depth() const { return depth_; }

    // Helper structures to parse settings when launching vkCmdBeginRendering
    VkResolveModeFlagBits get_color_resolve_mode(const uint32_t index) const { return resolve_modes_[index]; }
    VkImageLayout get_color_resolve_layout(const uint32_t index) const { return resolve_layouts_[index]; }

private:
    std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> colors_{};
    std::array<VkResolveModeFlagBits, kMaxColorAttachments> resolve_modes_{};
    std::array<VkImageLayout, kMaxColorAttachments> resolve_layouts_{};

    VkRenderingAttachmentInfo depth_{};
    uint32_t color_count_ = 0;
    bool has_depth_ = false;
};

struct FrameBuffer {
    std::array<Image *, Attachment::kMaxColorAttachments> color_images_{};
    std::array<Image *, Attachment::kMaxColorAttachments> resolve_images_{};
    Image *depth_image_;
};
