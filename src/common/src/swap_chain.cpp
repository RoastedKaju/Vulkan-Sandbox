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

    const VkSwapchainCreateInfoKHR swap_chain_create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = context->surface_,
        .minImageCount = surface_capabilities.minImageCount,
        .imageFormat = swap_chain_format_,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent{.width = swap_chain_extent.width, .height = swap_chain_extent.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
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
        swap_chain_images_[i].image = images[i];
        swap_chain_images_[i].format = swap_chain_format_;
        swap_chain_images_[i].aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        swap_chain_images_[i].width = swap_chain_extent.width;
        swap_chain_images_[i].height = swap_chain_extent.height;
    }

    for (auto i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swap_chain_images_[i].image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swap_chain_format_,
            .subresourceRange = {.aspectMask = swap_chain_images_[i].aspect, .levelCount = 1, .layerCount = 1}
        };
        check(vkCreateImageView(device, &image_view_create_info, nullptr, &swap_chain_images_[i].view));
    }

    std::printf("Swap-chain created.\n");
}

void SwapChain::setup_depth_attachment(const Context *context) {
    const auto physical_device = context->physical_device_;
    const auto device = context->device_;
    const auto window_size = context->window_size_;

    // ReSharper disable once CppTooWideScopeInitStatement
    std::vector<VkFormat> depth_format_list{VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    // ReSharper disable once CppLocalVariableMayBeConst
    for (VkFormat &format: depth_format_list) {
        VkFormatProperties2 format_properties{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        vkGetPhysicalDeviceFormatProperties2(physical_device, format, &format_properties);
        if (format_properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depth_image_.format = format;
            break;
        }
    }

    assert(depth_image_.format != VK_FORMAT_UNDEFINED && "Depth format is undefined.\n");
    const VkImageCreateInfo depth_image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_image_.format,
        .extent{
            .width = static_cast<uint32_t>(window_size.x), .height = static_cast<uint32_t>(window_size.y), .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    constexpr VmaAllocationCreateInfo depth_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    check(vmaCreateImage(
        context->allocator_,
        &depth_image_create_info,
        &depth_allocation_create_info,
        &depth_image_.image,
        &depth_image_allocation_, nullptr));
    const VkImageViewCreateInfo depth_view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image_.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depth_image_.format,
        .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
    };
    check(vkCreateImageView(device, &depth_view_create_info, nullptr, &depth_image_.view));

    std::printf("Depth attachment setup.\n");
}
