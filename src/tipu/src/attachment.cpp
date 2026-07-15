#include "attachment.h"

#include <cassert>

uint32_t Attachment::add_color(const VkImageLayout layout,
                               const VkAttachmentLoadOp load_op,
                               const VkAttachmentStoreOp store_op,
                               const VkClearColorValue clear,
                               const VkResolveModeFlagBits resolve_mode,
                               const VkImageLayout resolve_layout) {
    assert(color_count_ < kMaxColorAttachments && "exceeded max color attachments");

    colors_[color_count_] = VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = layout,
        .resolveMode = resolve_mode,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = resolve_layout,
        .loadOp = load_op,
        .storeOp = store_op,
        .clearValue = {.color = clear}
    };

    resolve_modes_[color_count_] = resolve_mode;
    resolve_layouts_[color_count_] = resolve_layout;

    return color_count_++;
}

void Attachment::set_depth(const VkImageLayout layout,
                           const VkAttachmentLoadOp load_op,
                           const VkAttachmentStoreOp store_op,
                           const VkClearDepthStencilValue clear) {
    depth_ = VkRenderingAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = layout,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = load_op,
        .storeOp = store_op,
        .clearValue = {.depthStencil = clear}
    };
    has_depth_ = true;
}
