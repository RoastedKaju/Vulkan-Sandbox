#include "context.h"

#include <cassert>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
// ReSharper disable once CppUnusedIncludeDirective
#include <vma/vk_mem_alloc.h>

#include "utils.h"
#include "buffer.h"
#include "importer.h"

// debug callback
// ReSharper disable once CppParameterMayBeConst
VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT type,
                                              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                              void *pUserData) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::printf("[Validation] %s\n", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

Context::Context(const Config &config) : config_(config) {
    check(SDL_Init(SDL_INIT_VIDEO));
    check(SDL_Vulkan_LoadLibrary(nullptr));
    volkInitialize();
}

Context::~Context() {
}

bool Context::initialize() {
    create_instance(config_.app_name_.c_str());
    setup_device();
    create_default_sampler();

    return true;
}

VkCommandBuffer Context::get_current_cmd_buf() const {
    const uint32_t frame_index = frame_data_.frame_index_;
    return frame_data_.command_buffers_[frame_index];
}

bool Context::create_instance(const char *app_name) {
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = app_name,
        .apiVersion = VK_API_VERSION_1_3
    };

    uint32_t instance_extension_count{0};
    char const *const *sdl_extensions{SDL_Vulkan_GetInstanceExtensions(&instance_extension_count)};
    std::vector<const char *> instance_extensions(sdl_extensions, sdl_extensions + instance_extension_count);
    if (config_.enable_validation_) {
        instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        ++instance_extension_count;
    }

    VkDebugUtilsMessengerCreateInfoEXT debug_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback
    };

    const VkInstanceCreateInfo instance_create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &debug_create_info,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = config_.enable_validation_ ? static_cast<uint32_t>(validation_layers_.size()) : 0,
        .ppEnabledLayerNames = config_.enable_validation_ ? validation_layers_.data() : nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data()
    };

    check(vkCreateInstance(&instance_create_info, nullptr, &instance_));
    volkLoadInstance(instance_);

    if (config_.enable_validation_) {
        check(vkCreateDebugUtilsMessengerEXT(instance_, &debug_create_info, nullptr, &debug_messenger_));
    }

    return true;
}

bool Context::setup_device(const uint32_t device_index) {
    // device
    uint32_t device_count{0};
    check(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count);
    check(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()));

    device_index_ = device_index;
    assert(device_index_ < device_count);

    VkPhysicalDeviceProperties2 device_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(devices[device_index], &device_properties);
    physical_device_ = devices[device_index];
    printf("Selected device: %s.\n", device_properties.properties.deviceName);
    assert(Attachment::kMaxColorAttachments <= device_properties.properties.limits.maxColorAttachments);

    // queue
    uint32_t queue_family_count{0};
    vkGetPhysicalDeviceQueueFamilyProperties(devices[device_index], &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[device_index], &queue_family_count, queue_families.data());
    for (auto i = 0; i < queue_families.size(); ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index_ = static_cast<uint32_t>(i);
            break;
        }
    }
    check(SDL_Vulkan_GetPresentationSupport(instance_, devices[device_index], queue_family_index_));

    // logical device
    constexpr float priorities = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family_index_,
        .queueCount = 1,
        .pQueuePriorities = &priorities,
    };

    VkPhysicalDeviceVulkan12Features enabled_features_12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
        .descriptorBindingSampledImageUpdateAfterBind = true,
        .descriptorBindingPartiallyBound = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true
    };
    VkPhysicalDeviceVulkan13Features enabled_features_13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabled_features_12,
        .synchronization2 = true,
        .dynamicRendering = true
    };
    VkPhysicalDeviceFeatures enabled_features_10{
        .fillModeNonSolid = VK_TRUE,
        .samplerAnisotropy = VK_TRUE,
        .shaderInt64 = VK_TRUE,
    };

    const std::vector<const char *> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_features_13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &enabled_features_10
    };

    check(vkCreateDevice(devices[device_index], &device_create_info, nullptr, &device_));
    volkLoadDevice(device_);

    vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);

    // VMA
    VmaVulkanFunctions vk_functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkCreateImage = vkCreateImage
    };
    VmaAllocatorCreateInfo allocator_create_info{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = devices[device_index],
        .device = device_,
        .pVulkanFunctions = &vk_functions,
        .instance = instance_
    };
    check(vmaCreateAllocator(&allocator_create_info, &allocator_));

    return true;
}

SDL_Window *Context::create_window(const char *title, const uint32_t width, const uint32_t height) {
    // create window and surface
    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(window_ && "Failed to create window");
    check(SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_));
    check(SDL_GetWindowSize(window_, &window_size_.x, &window_size_.y));

    // initialize bindless texture registry
    descriptor_registry_.init(device_);

    // initialize swap chain
    create_swap_chain();
    create_frame_resources();

    return window_;
}

std::unique_ptr<Image> Context::create_texture(const TextureDesc &desc) const {
    auto image = std::make_unique<Image>();
    image->width_ = desc.dimension_.x;
    image->height_ = desc.dimension_.y;
    image->depth_ = desc.depth_;
    image->format_ = desc.format_;
    image->aspect_ = desc.aspect_;
    image->type_ = desc.type_;
    image->usage_ = desc.usage_;
    image->mip_levels_ = desc.mip_levels_;
    image->array_layers_ = desc.array_layers_;
    image->samples_ = desc.samples_;

    const VkImageCreateInfo image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = image->type_,
        .format = image->format_,
        .extent{.width = image->width_, .height = image->height_, .depth = image->depth_},
        .mipLevels = image->mip_levels_,
        .arrayLayers = image->array_layers_,
        .samples = image->samples_,
        .tiling = desc.tiling_,
        .usage = image->usage_,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    const VmaAllocationCreateInfo alloc_create_info{
        .flags = desc.prefer_dedicated_alloc_ ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    check(vmaCreateImage(
        allocator_,
        &image_create_info,
        &alloc_create_info,
        &image->image_, &image->allocation_, nullptr));

    const VkImageViewCreateInfo view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image->format_,
        .subresourceRange{
            .aspectMask = image->aspect_,
            .levelCount = image->mip_levels_,
            .layerCount = image->array_layers_,
        }
    };
    check(vkCreateImageView(device_, &view_create_info, nullptr, &image->view_));

    return image;
}

std::unique_ptr<Image> Context::load_texture(const std::filesystem::path &path, const VkFormat format) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Texture does not exist: " + path.string());
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // ReSharper disable once CppTooWideScopeInitStatement
    unsigned char *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::printf("Failed to load texture: %s (%s)\n", path.string().c_str(), stbi_failure_reason());
        exit(EXIT_FAILURE);
    }

    const TextureDesc texture_desc{
        .dimension_ = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
        .depth_ = 1,
        .mip_levels_ = 1,
        .array_layers_ = 1,
        .samples_ = VK_SAMPLE_COUNT_1_BIT,
        .tiling_ = VK_IMAGE_TILING_OPTIMAL,
        .usage_ = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect_ = VK_IMAGE_ASPECT_COLOR_BIT,
        .format_ = format,
        .prefer_dedicated_alloc_ = false
    };

    auto image = create_texture(texture_desc);

    // Upload pixel data
    const VkDeviceSize data_size = static_cast<VkDeviceSize>(width) * height * 4;
    const BufferDesc buf_desc{
        .context = this,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = data_size,
        .per_frame = false
    };
    Buffer data_buffer{};
    data_buffer.create(buf_desc);
    data_buffer.update(pixels);

    // free image data from CPU
    stbi_image_free(pixels);

    constexpr VkFenceCreateInfo fence_one_time_create_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence_one_time{};
    check(vkCreateFence(device_, &fence_one_time_create_info, nullptr, &fence_one_time));
    VkCommandBuffer cmd_buf_one_time{};
    const VkCommandBufferAllocateInfo cmd_buf_alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .commandBufferCount = 1
    };
    check(vkAllocateCommandBuffers(device_, &cmd_buf_alloc_info, &cmd_buf_one_time));
    constexpr VkCommandBufferBeginInfo cmd_buf_one_time_begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    check(vkBeginCommandBuffer(cmd_buf_one_time, &cmd_buf_one_time_begin));

    transition_image(cmd_buf_one_time,
                     *image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    const VkBufferImageCopy copy_region{
        .bufferOffset = 0,
        .imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageExtent{
            .width = static_cast<uint32_t>(texture_desc.dimension_[0]),
            .height = static_cast<uint32_t>(texture_desc.dimension_[1]), .depth = 1
        }
    };
    vkCmdCopyBufferToImage(
        cmd_buf_one_time,
        data_buffer.get(),
        image->image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy_region);

    transition_image(cmd_buf_one_time,
                     *image,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    check(vkEndCommandBuffer(cmd_buf_one_time));

    const VkSubmitInfo one_time_submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf_one_time
    };
    check(vkQueueSubmit(queue_, 1, &one_time_submit_info, fence_one_time));
    check(vkWaitForFences(device_, 1, &fence_one_time, VK_TRUE, UINT64_MAX));

    vkDestroyFence(device_, fence_one_time, nullptr);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd_buf_one_time);
    vmaDestroyBuffer(allocator_, data_buffer.get(), data_buffer.get_allocation());

    // Register into bindless descriptor array, store resulting slot on the image
    image->bindless_index_ = descriptor_registry_.register_texture(image->view_, default_sampler_);

    return image;
}

std::unique_ptr<Image> Context::load_cubemap(const std::array<std::filesystem::path, 6> &paths) {
    int width = 0;
    int height = 0;
    std::array<unsigned char *, 6> face_pixels{};

    for (size_t i = 0; i < 6; ++i) {
        if (!std::filesystem::exists(paths[i])) {
            throw std::runtime_error("Cubemap face does not exist: " + paths[i].string());
        }

        int w = 0, h = 0, c = 0;
        unsigned char *pixels = stbi_load(paths[i].string().c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!pixels) {
            std::printf("Failed to load cubemap face: %s (%s)\n", paths[i].string().c_str(), stbi_failure_reason());
            exit(EXIT_FAILURE);
        }

        if (i == 0) {
            width = w;
            height = h;
        } else if (w != width || h != height) {
            throw std::runtime_error("Cubemap face size mismatch: " + paths[i].string());
        }

        face_pixels[i] = pixels;
    }

    // create cubemap
    auto image = std::make_unique<Image>();
    image->format_ = VK_FORMAT_R8G8B8A8_UNORM;
    image->aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    image->type_ = VK_IMAGE_TYPE_2D;
    image->usage_ = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image->width_ = static_cast<uint32_t>(width);
    image->height_ = static_cast<uint32_t>(height);
    image->depth_ = 1;
    image->mip_levels_ = 1;
    image->array_layers_ = 6;
    image->samples_ = VK_SAMPLE_COUNT_1_BIT;

    const VkImageCreateInfo image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image->format_,
        .extent = {image->width_, image->height_, 1},
        .mipLevels = image->mip_levels_,
        .arrayLayers = image->array_layers_,
        .samples = image->samples_,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = image->usage_,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_create_info{};
    alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;

    check(vmaCreateImage(allocator_, &image_create_info, &alloc_create_info,
                         &image->image_, &image->allocation_, nullptr));

    const VkImageViewCreateInfo view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image_,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format = image->format_,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6,
        }
    };

    check(vkCreateImageView(device_, &view_create_info, nullptr, &image->view_));

    // upload all 6 faces
    const VkDeviceSize face_size = static_cast<VkDeviceSize>(width) * height * 4;
    const VkDeviceSize total_size = face_size * 6;

    const BufferDesc buf_desc{
        .context = this,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = total_size,
        .per_frame = false
    };
    Buffer data_buffer{};
    data_buffer.create(buf_desc);

    // Copy each face's pixels into its region of the staging buffer, then free CPU copy
    for (size_t i = 0; i < 6; ++i) {
        data_buffer.update(face_pixels[i], face_size, face_size * i);
        stbi_image_free(face_pixels[i]);
    }

    constexpr VkFenceCreateInfo fence_one_time_create_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence_one_time{};
    check(vkCreateFence(device_, &fence_one_time_create_info, nullptr, &fence_one_time));
    VkCommandBuffer cmd_buf_one_time{};
    const VkCommandBufferAllocateInfo cmd_buf_alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .commandBufferCount = 1
    };

    check(vkAllocateCommandBuffers(device_, &cmd_buf_alloc_info, &cmd_buf_one_time));
    constexpr VkCommandBufferBeginInfo cmd_buf_one_time_begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    check(vkBeginCommandBuffer(cmd_buf_one_time, &cmd_buf_one_time_begin));

    transition_image(cmd_buf_one_time,
                     *image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    // One copy region per face/layer; all six can be issued in a single vkCmdCopyBufferToImage call
    std::array<VkBufferImageCopy, 6> copy_regions{};
    for (uint32_t i = 0; i < 6; ++i) {
        copy_regions[i] = VkBufferImageCopy{
            .bufferOffset = face_size * i,
            .imageSubresource{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = i,
                .layerCount = 1
            },
            .imageExtent{.width = image->width_, .height = image->height_, .depth = 1}
        };
    }

    vkCmdCopyBufferToImage(
        cmd_buf_one_time,
        data_buffer.get(),
        image->image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(copy_regions.size()),
        copy_regions.data());

    transition_image(cmd_buf_one_time,
                     *image,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    check(vkEndCommandBuffer(cmd_buf_one_time));

    const VkSubmitInfo one_time_submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf_one_time
    };
    check(vkQueueSubmit(queue_, 1, &one_time_submit_info, fence_one_time));
    check(vkWaitForFences(device_, 1, &fence_one_time, VK_TRUE, UINT64_MAX));

    vkDestroyFence(device_, fence_one_time, nullptr);
    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd_buf_one_time);
    vmaDestroyBuffer(allocator_, data_buffer.get(), data_buffer.get_allocation());

    // Register into the CUBE bindless array (separate binding/array from sampler2D)
    image->bindless_index_ = descriptor_registry_.register_cubemap(image->view_, default_sampler_);

    return image;
}

VkFormat Context::get_device_depth_format() const {
    // ReSharper disable once CppTooWideScopeInitStatement
    std::vector<VkFormat> depth_format_list{VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    // ReSharper disable once CppLocalVariableMayBeConst
    for (VkFormat &format: depth_format_list) {
        VkFormatProperties2 format_properties{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        vkGetPhysicalDeviceFormatProperties2(physical_device_, format, &format_properties);
        if (format_properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

void Context::acquire_command_buffer() {
    const VkSwapchainKHR swap_chain = swap_chain_.get();
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto &fences = frame_data_.fences_;
    const auto &image_acquire_semaphores = frame_data_.image_acquired_semaphores_;

    // sync
    check(vkWaitForFences(device_, 1, &fences[frame_index], true, UINT64_MAX));
    check(vkResetFences(device_, 1, &fences[frame_index]));
    const VkResult acquire_next_image_result = vkAcquireNextImageKHR(device_,
                                                                     swap_chain,
                                                                     UINT64_MAX,
                                                                     image_acquire_semaphores[frame_index],
                                                                     VK_NULL_HANDLE, &frame_data_.image_index_);

    if (acquire_next_image_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_next_image_result == VK_SUBOPTIMAL_KHR) {
        swap_chain_.mark_swap_chain_dirty();
    } else if (acquire_next_image_result != VK_SUCCESS) {
        printf("Swap-chain check failed %d\n", acquire_next_image_result);
        exit(EXIT_FAILURE);
    }
}

void Context::begin_rendering(const Attachment &attachment, const FrameBuffer &frame_buffer) const {
    const uint32_t frame_index = frame_data_.frame_index_;

    const auto cmd = frame_data_.command_buffers_[frame_index];

    check(vkResetCommandBuffer(cmd, 0));

    constexpr VkCommandBufferBeginInfo cmd_begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    check(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    // transition for depth image
    if (attachment.has_depth()) {
        transition_image(cmd, *frame_buffer.depth_image_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    }

    // transition for color attachments
    for (uint32_t i = 0; i < attachment.color_count(); ++i) {
        if (frame_buffer.color_images_[i]) {
            transition_image(cmd, *frame_buffer.color_images_[i], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        }
    }

    // rendering attachments
    std::array<VkRenderingAttachmentInfo, Attachment::kMaxColorAttachments> resolved_colors{};
    for (uint32_t i = 0; i < attachment.color_count(); ++i) {
        resolved_colors[i] = attachment.color(i);
        resolved_colors[i].imageView = frame_buffer.color_images_[i]->view_;
    }

    VkRenderingAttachmentInfo resolved_depth = attachment.depth();
    if (attachment.has_depth()) {
        resolved_depth.imageView = frame_buffer.depth_image_->view_;
    }

    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{
            .extent{
                .width = static_cast<uint32_t>(window_size_.x),
                .height = static_cast<uint32_t>(window_size_.y)
            }
        },
        .layerCount = 1,
        .colorAttachmentCount = attachment.color_count(),
        .pColorAttachments = resolved_colors.data(),
        .pDepthAttachment = attachment.has_depth() ? &resolved_depth : nullptr,
    };

    vkCmdBeginRendering(cmd, &rendering_info);

    // default viewport/scissor — set every time we begin rendering so callers
    // don't have to think about it unless they want to override
    const VkViewport viewport{
        .width = static_cast<float>(window_size_.x),
        .height = static_cast<float>(window_size_.y),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const VkRect2D scissor{
        .extent{
            .width = static_cast<uint32_t>(window_size_.x),
            .height = static_cast<uint32_t>(window_size_.y)
        }
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Context::bind_pipeline(const VkPipeline pipeline) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void Context::bind_descriptor_set(const PipelineLayout &pipeline_layout, const VkDescriptorSet descriptor_set) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout.layout_, 0, 1, &descriptor_set, 0,
                            nullptr);
}

void Context::bind_vertex_buffer(const VkBuffer buffer) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    constexpr VkDeviceSize vertex_offset{0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &vertex_offset);
}

void Context::bind_index_buffer(const VkBuffer buffer) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdBindIndexBuffer(cmd, buffer, 0, VK_INDEX_TYPE_UINT32);
}

void Context::cmd_push_constants(const PipelineLayout &pipeline_layout, const void *data) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdPushConstants(cmd,
                       pipeline_layout.layout_,
                       pipeline_layout.shader_stage_flags_,
                       pipeline_layout.offset_,
                       pipeline_layout.size_, data);
}

void Context::draw_indexed(const uint32_t index_count) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

void Context::draw(const uint32_t vertex_count) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
}

void Context::end_rendering() const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdEndRendering(cmd);
}

void Context::submit() {
    const uint32_t frame_index = frame_data_.frame_index_;
    const uint32_t image_index = frame_data_.image_index_;
    auto &current_swap_chain_image = swap_chain_.get_images()[image_index];

    const auto cmd = frame_data_.command_buffers_[frame_index];
    auto swap_chain = swap_chain_.get();

    // transition to present
    transition_image(cmd,
                     current_swap_chain_image,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    check(vkEndCommandBuffer(cmd));

    // submit
    constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame_data_.image_acquired_semaphores_[frame_index],
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &frame_data_.render_complete_semaphores_[image_index]
    };
    check(vkQueueSubmit(queue_, 1, &submit_info, frame_data_.fences_[frame_index]));

    frame_data_.frame_index_ = (frame_index + 1) % frame_data_.max_frames_in_flight_;

    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame_data_.render_complete_semaphores_[image_index],
        .swapchainCount = 1,
        .pSwapchains = &swap_chain,
        .pImageIndices = &image_index
    };
    vkQueuePresentKHR(queue_, &present_info);
}

void Context::update_window_size() {
    check(SDL_GetWindowSize(window_, &window_size_.x, &window_size_.y));
}

void Context::recreate_swap_chain() {
    swap_chain_.recreate_swap_chain(this);
}

void Context::wait_idle() {
    check(vkDeviceWaitIdle(device_));

    // TODO: Move these somewhere else
    for (auto i = 0; i < frame_data_.max_frames_in_flight_; ++i) {
        vkDestroyFence(device_, frame_data_.fences_[i], nullptr);
        vkDestroySemaphore(device_, frame_data_.image_acquired_semaphores_[i], nullptr);
    }
    for (auto i = 0; i < frame_data_.render_complete_semaphores_.size(); ++i) {
        vkDestroySemaphore(device_, frame_data_.render_complete_semaphores_[i], nullptr);
    }
    for (auto i = 0; i < swap_chain_.get_images().size(); ++i) {
        vkDestroyImageView(device_, swap_chain_.get_images()[i].view_, nullptr);
    }

    // destroy texture registry
    vkDestroyDescriptorSetLayout(device_, descriptor_registry_.get_layout(), nullptr);
    vkDestroyDescriptorPool(device_, descriptor_registry_.get_pool(), nullptr);

    vkDestroySwapchainKHR(device_, swap_chain_.get(), nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyCommandPool(device_, command_pool_, nullptr);

    vkDestroySampler(device_, default_sampler_, nullptr);
}

void Context::destroy_pipeline_layout(const PipelineLayout &layout) const {
    vkDestroyPipelineLayout(device_, layout.layout_, nullptr);
}

void Context::destroy_pipeline(const VkPipeline pipeline) const {
    vkDestroyPipeline(device_, pipeline, nullptr);
}

void Context::destroy_image(const Image *image) const {
    if (image->view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image->view_, nullptr);
    }
    if (image->image_ != VK_NULL_HANDLE && image->allocation_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image->image_, image->allocation_);
    }
}

void Context::destory_shader(const VkShaderModule shader_module) const {
    vkDestroyShaderModule(device_, shader_module, nullptr);
}

void Context::destroy() const {
    vmaDestroyAllocator(allocator_);

    SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    SDL_Quit();

    vkDestroyDevice(device_, nullptr);
    vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

Image *Context::get_current_swap_chain_image() {
    const uint32_t image_index = frame_data_.image_index_;
    return &swap_chain_.get_images()[image_index];
}

void Context::create_swap_chain() {
    swap_chain_.init_swap_chain(this);
}

void Context::create_frame_resources() {
    // setup sync objects
    constexpr VkSemaphoreCreateInfo semaphore_create_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    constexpr VkFenceCreateInfo fence_create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    for (auto i = 0; i < frame_data_.max_frames_in_flight_; ++i) {
        check(vkCreateFence(device_, &fence_create_info, nullptr, &frame_data_.fences_[i]));
        check(vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &frame_data_.image_acquired_semaphores_[i]));
    }
    frame_data_.render_complete_semaphores_.resize(swap_chain_.get_images().size());
    for (auto &semaphore: frame_data_.render_complete_semaphores_) {
        check(vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &semaphore));
    }

    // create command pool
    const VkCommandPoolCreateInfo command_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index_
    };
    check(vkCreateCommandPool(device_, &command_pool_create_info, nullptr, &command_pool_));
    const VkCommandBufferAllocateInfo command_buffer_allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool_,
        .commandBufferCount = frame_data_.max_frames_in_flight_
    };
    check(vkAllocateCommandBuffers(device_, &command_buffer_allocate_info, frame_data_.command_buffers_.data()));
    std::printf("Command pool and buffers created.\n");
}

void Context::transition_image(const VkCommandBuffer cmd,
                               Image &image,
                               const VkImageLayout new_layout,
                               const VkAccessFlags2 new_access,
                               const VkPipelineStageFlags2 new_stage) {
    const VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = image.state_.stage_,
        .srcAccessMask = image.state_.access_,
        .dstStageMask = new_stage,
        .dstAccessMask = new_access,
        .oldLayout = image.state_.layout_,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image_,
        .subresourceRange{
            .aspectMask = image.aspect_,
            .levelCount = image.mip_levels_,
            .layerCount = image.array_layers_,
        }
    };
    const VkDependencyInfo dep_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(cmd, &dep_info);

    image.state_ = ImageState{new_layout, new_access, new_stage};
}

void Context::create_default_sampler() {
    // Sampler
    constexpr VkSamplerCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    check(vkCreateSampler(device_, &create_info, nullptr, &default_sampler_));
}
