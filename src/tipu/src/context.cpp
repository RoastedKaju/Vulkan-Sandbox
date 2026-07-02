#include "context.h"

#include <cassert>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "utils.h"
#include "buffer.h"

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
        .samplerAnisotropy = VK_TRUE,
        .shaderInt64 = VK_TRUE
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
    SDL_Window *window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    assert(window && "Failed to create window");
    check(SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_));
    check(SDL_GetWindowSize(window, &window_size_.x, &window_size_.y));

    // initialize bindless texture registry
    descriptor_registry_.init(device_);

    // initialize swap chain
    create_swap_chain();
    create_frame_resources();

    return window;
}

std::unique_ptr<Image> Context::create_texture(const TextureDesc &desc) const {
    auto image = std::make_unique<Image>();
    image->width = desc.dimension_.x;
    image->height = desc.dimension_.y;
    image->format = desc.format_;
    image->aspect = desc.aspect_;

    const VkImageCreateInfo image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image->format,
        .extent{.width = image->width, .height = image->height, .depth = desc.depth_},
        .mipLevels = desc.mip_levels_,
        .arrayLayers = desc.array_layers_,
        .samples = desc.samples_,
        .tiling = desc.tiling_,
        .usage = desc.usage_,
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
        &image->image, &image->allocation, nullptr));

    const VkImageViewCreateInfo view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image->format,
        .subresourceRange{
            .aspectMask = desc.aspect_,
            .levelCount = desc.mip_levels_,
            .layerCount = desc.array_layers_,
        }
    };
    check(vkCreateImageView(device_, &view_create_info, nullptr, &image->view));

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
    if (acquire_next_image_result < VK_SUCCESS) {
        if (acquire_next_image_result == VK_ERROR_OUT_OF_DATE_KHR) {
            swap_chain_.mark_swap_chain_dirty();
        } else {
            printf("Swap-chain check failed %d\b", acquire_next_image_result);
            exit(EXIT_FAILURE);
        }
    }
}

void Context::begin_rendering(const Attachment &attachment, const FrameBuffer &frame_buffer) {
    const uint32_t frame_index = frame_data_.frame_index_;
    const uint32_t image_index = frame_data_.image_index_;
    const VkImage current_swap_chain_image = swap_chain_.get_images()[image_index].image;
    ImageState &current_swap_chain_image_state = swap_chain_.get_images()[image_index].state;
    ImageState &current_depth_image_state = swap_chain_.get_depth_image().state;

    const auto cmd = frame_data_.command_buffers_[frame_index];

    check(vkResetCommandBuffer(cmd, 0));

    constexpr VkCommandBufferBeginInfo cmd_begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    check(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    // for swap chain image
    transition_image(cmd, current_swap_chain_image, current_swap_chain_image_state,
                     VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    // for depth image
    if (attachment.has_depth()) {
        transition_image(cmd, frame_buffer.depth_image_->image,
                         current_depth_image_state,
                         VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                         VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
    }

    // rendering attachments
    std::array<VkRenderingAttachmentInfo, Attachment::kMaxColorAttachments> resolved_colors{};
    for (uint32_t i = 0; i < attachment.color_count(); ++i) {
        resolved_colors[i] = attachment.color(i);
        resolved_colors[i].imageView = swap_chain_.get_images()[image_index].view;
    }

    VkRenderingAttachmentInfo resolved_depth = attachment.depth();
    if (attachment.has_depth()) {
        resolved_depth.imageView = frame_buffer.depth_image_->view;
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

void Context::bind_descriptor_set(const VkPipelineLayout pipeline_layout, const VkDescriptorSet descriptor_set) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
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

void Context::cmd_push_constants(const VkPipelineLayout pipeline_layout, const VkDeviceAddress address) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdPushConstants(cmd,
                       pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(VkDeviceAddress), &address);
}

void Context::draw_indexed(const uint32_t index_count) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

void Context::draw(uint32_t vertex_count) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void Context::end_rendering() const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdEndRendering(cmd);
}

void Context::submit() {
    const uint32_t frame_index = frame_data_.frame_index_;
    const uint32_t image_index = frame_data_.image_index_;
    const auto current_swap_chain_image = swap_chain_.get_images()[image_index].image;
    auto &current_swap_chain_image_state = swap_chain_.get_images()[image_index].state;

    const auto cmd = frame_data_.command_buffers_[frame_index];

    auto swap_chain = swap_chain_.get();

    // transition to present
    transition_image(cmd, current_swap_chain_image, current_swap_chain_image_state,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     0,
                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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

std::unique_ptr<Image> Context::load_texture(const std::filesystem::path &path, const glm::ivec3 &color) {
    const auto raw_color_data = make_solid_color_texture(4, 4, color.r, color.g, color.b);

    constexpr TextureDesc texture_desc{
        .dimension_ = {4, 4},
        .depth_ = 1,
        .mip_levels_ = 1,
        .array_layers_ = 1,
        .samples_ = VK_SAMPLE_COUNT_1_BIT,
        .tiling_ = VK_IMAGE_TILING_OPTIMAL,
        .usage_ = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .aspect_ = VK_IMAGE_ASPECT_COLOR_BIT,
        .format_ = VK_FORMAT_R8G8B8A8_UNORM,
        .prefer_dedicated_alloc_ = false
    };

    auto image = create_texture(texture_desc);

    // Upload pixel data
    const BufferDesc buf_desc{
        .context = this,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = raw_color_data.size(),
        .per_frame = false
    };
    Buffer data_buffer{};
    data_buffer.create(buf_desc);
    data_buffer.update(raw_color_data.data());

    constexpr VkFenceCreateInfo fence_one_time_create_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence_one_time{};
    check(vkCreateFence(device_, &fence_one_time_create_info, nullptr, &fence_one_time));
    VkCommandBuffer cmd_buf_one_time{};
    VkCommandBufferAllocateInfo cmd_buf_alloc_info{
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

    transition_image(cmd_buf_one_time, image->image, image->state,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT); // TODO: Add level count depending on image

    constexpr VkBufferImageCopy copy_region{
        .bufferOffset = 0,
        .imageSubresource{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageExtent{.width = texture_desc.dimension_[0], .height = texture_desc.dimension_[1], .depth = 1}
    };
    vkCmdCopyBufferToImage(
        cmd_buf_one_time,
        data_buffer.get(),
        image->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copy_region);

    transition_image(cmd_buf_one_time, image->image, image->state,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_2_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_IMAGE_ASPECT_COLOR_BIT);

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
    image->index = descriptor_registry_.register_texture(image->view, default_sampler_);

    return image;
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
                               const VkImage image,
                               ImageState &state,
                               const VkImageLayout new_layout,
                               const VkAccessFlags2 new_access,
                               const VkPipelineStageFlags2 new_stage,
                               const VkImageAspectFlags aspect,
                               const uint32_t level_count,
                               const uint32_t layer_count,
                               const uint32_t base_mip_level) {
    if (state.layout == new_layout && state.access == new_access) {
        return;
    }

    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = state.stage,
        .srcAccessMask = state.access,
        .dstStageMask = new_stage,
        .dstAccessMask = new_access,
        .oldLayout = state.layout,
        .newLayout = new_layout,
        .image = image,
        .subresourceRange{
            .aspectMask = aspect,
            .baseMipLevel = base_mip_level,
            .levelCount = level_count,
            .baseArrayLayer = 0,
            .layerCount = layer_count
        }
    };
    const VkDependencyInfo dep_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(cmd, &dep_info);

    state = {new_layout, new_access, new_stage};
}

void Context::create_default_sampler() {
    // Sampler
    constexpr VkSamplerCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f,
        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    check(vkCreateSampler(device_, &create_info, nullptr, &default_sampler_));
}
