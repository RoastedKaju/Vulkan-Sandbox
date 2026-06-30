#include "context.h"

#include <cassert>

#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "utils.h"

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

    // initialize swap chain
    create_swap_chain();
    create_frame_resources();

    return window;
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

void Context::begin_rendering() {
    const uint32_t frame_index = frame_data_.frame_index_;
    const uint32_t image_index = frame_data_.image_index_;
    const auto current_swap_chain_image = swap_chain_.get_images()[image_index];
    auto &current_swap_chain_image_state = swap_chain_.get_states()[image_index];
    auto &current_depth_image_state = swap_chain_.get_depth_state();

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
    transition_image(cmd, swap_chain_.get_depth_image(),
                     current_depth_image_state,
                     VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);

    // rendering attachments
    const VkRenderingAttachmentInfo color_attachment_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swap_chain_.get_views()[image_index],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {0.0f, 0.0f, 0.0f, 1.0f}}
    };
    const VkRenderingAttachmentInfo depth_attachment_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = swap_chain_.get_depth_image_view(),
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {1.0f, 0}}
    };
    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea{
            .extent{
                .width = static_cast<uint32_t>(window_size_.x),
                .height = static_cast<uint32_t>(window_size_.y)
            }
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_info,
        .pDepthAttachment = &depth_attachment_info,
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
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkDeviceAddress), &address);
}

void Context::draw_indexed(const uint32_t index_count) const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

void Context::end_rendering() const {
    const uint32_t frame_index = frame_data_.frame_index_;
    const auto cmd = frame_data_.command_buffers_[frame_index];
    vkCmdEndRendering(cmd);
}

void Context::submit() {
    const uint32_t frame_index = frame_data_.frame_index_;
    const uint32_t image_index = frame_data_.image_index_;
    const auto current_swap_chain_image = swap_chain_.get_images()[image_index];
    auto &current_swap_chain_image_state = swap_chain_.get_states()[image_index];

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

void Context::create_swap_chain() {
    swap_chain_.init_swap_chain(this);
    swap_chain_.setup_depth_attachment(this);
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
                               const VkImageAspectFlags aspect) {
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
        .subresourceRange{.aspectMask = aspect, .levelCount = 1, .layerCount = 1}
    };
    const VkDependencyInfo dep_info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(cmd, &dep_info);

    state = {new_layout, new_access, new_stage};
}
