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
    texture_descriptor_set_.create(device_, config_.max_texture_count_);

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
