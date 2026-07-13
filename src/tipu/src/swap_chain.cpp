#include "swap_chain.h"

#include <volk.h>

#include "context.h"
#include "utils.h"

void SwapChain::init_swap_chain(const Context *context, const VkFormat image_format) {
    swap_chain_format_ = image_format;
    VkSurfaceCapabilitiesKHR surface_capabilities{};
    const auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device_,
                                                                  context->surface_,
                                                                  &surface_capabilities);
    check(result);

    VkExtent2D swap_chain_extent{surface_capabilities.currentExtent};
    if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
        swap_chain_extent = {
            .width = static_cast<uint32_t>(context->window_size_.x),
            .height = static_cast<uint32_t>(context->window_size_.y)
        };
    }

    const auto device = context->device_;

    // clamp min image count
    uint32_t min_img_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0) {
        min_img_count = std::min(min_img_count, surface_capabilities.maxImageCount);
    }

    const VkSwapchainCreateInfoKHR swap_chain_create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = context->surface_,
        .minImageCount = min_img_count,
        .imageFormat = swap_chain_format_,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent{.width = swap_chain_extent.width, .height = swap_chain_extent.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = context->config_.present_mode_
    };
    check(vkCreateSwapchainKHR(device, &swap_chain_create_info, nullptr, &swap_chain_));
    uint32_t image_count{0};
    check(vkGetSwapchainImagesKHR(device, swap_chain_, &image_count, nullptr));
    swap_chain_images_.resize(image_count);

    std::vector<VkImage> images(image_count);
    check(vkGetSwapchainImagesKHR(device, swap_chain_, &image_count, images.data()));

    // copy the image handles back to our image wrapper
    for (uint32_t i = 0; i < image_count; ++i) {
        swap_chain_images_[i].image_ = images[i];
        swap_chain_images_[i].format_ = swap_chain_format_;
        swap_chain_images_[i].aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_chain_images_[i].width_ = swap_chain_extent.width;
        swap_chain_images_[i].height_ = swap_chain_extent.height;
    }

    for (auto i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swap_chain_images_[i].image_,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swap_chain_format_,
            .subresourceRange = {.aspectMask = swap_chain_images_[i].aspect_, .levelCount = 1, .layerCount = 1}
        };
        check(vkCreateImageView(device, &image_view_create_info, nullptr, &swap_chain_images_[i].view_));
    }

    surface_caps_ = surface_capabilities;

    std::printf("Swap-chain created.\n");
}

void SwapChain::recreate_swap_chain(Context *context) {
    is_swap_chain_dirty_ = false;

    context->update_window_size();

    check(vkDeviceWaitIdle(context->device_));
    VkSurfaceCapabilitiesKHR surface_capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context->physical_device_,
                                                    context->surface_,
                                                    &surface_capabilities));

    // clamp min image count
    uint32_t min_img_count = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount > 0) {
        min_img_count = std::min(min_img_count, surface_capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR swap_chain_create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = context->surface_,
        .minImageCount = min_img_count,
        .imageFormat = swap_chain_format_,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent{
            .width = static_cast<uint32_t>(context->window_size_.x),
            .height = static_cast<uint32_t>(context->window_size_.y)
        },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = context->config_.present_mode_
    };
    swap_chain_create_info.oldSwapchain = swap_chain_;

    check(vkCreateSwapchainKHR(context->device_, &swap_chain_create_info, nullptr, &swap_chain_));
    // destroy old image views
    for (auto i = 0; i < swap_chain_images_.size(); i++) {
        vkDestroyImageView(context->device_, swap_chain_images_[i].view_, nullptr);
    }

    uint32_t image_count{0};
    check(vkGetSwapchainImagesKHR(context->device_, swap_chain_, &image_count, nullptr));
    std::vector<VkImage> images(image_count);
    check(vkGetSwapchainImagesKHR(context->device_, swap_chain_, &image_count, images.data()));
    swap_chain_images_.resize(image_count);

    // copy the image handles back to our image wrapper
    for (uint32_t i = 0; i < image_count; ++i) {
        swap_chain_images_[i].image_ = images[i];
        swap_chain_images_[i].format_ = swap_chain_format_;
        swap_chain_images_[i].aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_chain_images_[i].width_ = context->window_size_.x;
        swap_chain_images_[i].height_ = context->window_size_.y;
        swap_chain_images_[i].state_ = ImageState{};
    }

    // create new views
    for (auto i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swap_chain_images_[i].image_,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swap_chain_format_,
            .subresourceRange = {.aspectMask = swap_chain_images_[i].aspect_, .levelCount = 1, .layerCount = 1}
        };
        check(vkCreateImageView(context->device_, &image_view_create_info, nullptr, &swap_chain_images_[i].view_));
    }

    // destroy sync objects
    for (const auto &semaphore: context->frame_data_.render_complete_semaphores_) {
        vkDestroySemaphore(context->device_, semaphore, nullptr);
    }
    context->frame_data_.render_complete_semaphores_.resize(image_count);
    constexpr VkSemaphoreCreateInfo semaphore_create_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto &semaphore: context->frame_data_.render_complete_semaphores_) {
        check(vkCreateSemaphore(context->device_, &semaphore_create_info, nullptr, &semaphore));
    }

    // destroy old swap chain, depth image and view
    vkDestroySwapchainKHR(context->device_, swap_chain_create_info.oldSwapchain, nullptr);
}
