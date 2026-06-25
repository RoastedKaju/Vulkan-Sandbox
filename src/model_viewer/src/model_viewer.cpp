#include <iostream>
#include <vector>
#include <array>
#include <cassert>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include "glm/vec2.hpp"

constexpr bool kHasValidationLayer = true;
const std::vector<const char *> validation_layers = {"VK_LAYER_KHRONOS_validation"};
VkDebugUtilsMessengerEXT debug_messenger;

// debug callback
// ReSharper disable once CppParameterMayBeConst
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT type,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                     void *pUserData) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::printf("[Validation] %s\n", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

constexpr uint32_t kMaxFramesInFlight = 2;
uint32_t image_index{0};
uint32_t frame_index{0};

glm::ivec2 window_size{};

VkInstance instance{VK_NULL_HANDLE};
VkDevice device{VK_NULL_HANDLE};
VkQueue queue{VK_NULL_HANDLE};
VkSurfaceKHR surface{VK_NULL_HANDLE};
bool is_swap_chain_dirty{false};
VkSwapchainKHR swap_chain{VK_NULL_HANDLE};
VkCommandPool command_pool{VK_NULL_HANDLE};
VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
VkPipeline pipeline{VK_NULL_HANDLE};
VkImage depth_image{VK_NULL_HANDLE};
VkImageView depth_image_view{VK_NULL_HANDLE};
VmaAllocator allocator{VK_NULL_HANDLE};
VmaAllocation depth_image_allocation{VK_NULL_HANDLE};

std::vector<VkImage> swap_chain_images;
std::vector<VkImageView> swap_chain_image_views;
std::array<VkCommandBuffer, kMaxFramesInFlight> command_buffers;
std::array<VkFence, kMaxFramesInFlight> fences;
std::array<VkSemaphore, kMaxFramesInFlight> image_acquired_semaphores;
std::vector<VkSemaphore> render_complete_semaphores;

VmaAllocation vertex_buffer_allocation{VK_NULL_HANDLE};
VkBuffer vertex_buffer{VK_NULL_HANDLE};

static void check(const VkResult result) {
    if (result != VK_SUCCESS) {
        printf("Vulkan call returned an error: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

static void check(const bool result) {
    if (!result) {
        printf("Call returned an error\n");
        exit(EXIT_FAILURE);
    }
}

static void check_swap_chain(const VkResult result) {
    if (result < VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            is_swap_chain_dirty = true;
            return;
        }
        printf("Swap-chain check failed %d\b", result);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    check(SDL_Init(SDL_INIT_VIDEO));
    check(SDL_Vulkan_LoadLibrary(nullptr));
    volkInitialize();

    // create instance
    VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Model Viewer",
        .apiVersion = VK_API_VERSION_1_3
    };

    uint32_t instance_extension_count{0};
    char const *const *sdl_extensions{SDL_Vulkan_GetInstanceExtensions(&instance_extension_count)};
    std::vector<const char *> instance_extensions(sdl_extensions, sdl_extensions + instance_extension_count);
    if (kHasValidationLayer) {
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
        .enabledLayerCount = static_cast<uint32_t>(validation_layers.size()),
        .ppEnabledLayerNames = validation_layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(instance_extensions.size()),
        .ppEnabledExtensionNames = instance_extensions.data()
    };

    check(vkCreateInstance(&instance_create_info, nullptr, &instance));
    volkLoadInstance(instance);
    std::printf("Instance created.\n");

    if (kHasValidationLayer) {
        check(vkCreateDebugUtilsMessengerEXT(instance, &debug_create_info, nullptr, &debug_messenger));
        std::printf("Debug messenger created.\n");
    }

    // device
    uint32_t device_count{0};
    check(vkEnumeratePhysicalDevices(instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count);
    check(vkEnumeratePhysicalDevices(instance, &device_count, devices.data()));
    uint32_t device_index{0};
    if (argc > 1) {
        device_index = std::stoi(argv[1]);
        assert(device_index < device_count);
    }
    VkPhysicalDeviceProperties2 device_properties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vkGetPhysicalDeviceProperties2(devices[device_index], &device_properties);
    printf("Selected device: %s\n", device_properties.properties.deviceName);

    // queue
    uint32_t queue_family_count{0};
    vkGetPhysicalDeviceQueueFamilyProperties(devices[device_index], &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(devices[device_index], &queue_family_count, queue_families.data());
    uint32_t queue_family_index{0};
    for (auto i = 0; i < queue_families.size(); ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            queue_family_index = static_cast<uint32_t>(i);
            break;
        }
    }
    check(SDL_Vulkan_GetPresentationSupport(instance, devices[device_index], queue_family_index));

    // logical device
    constexpr float priorities = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &priorities,
    };

    VkPhysicalDeviceVulkan12Features enabled_features_12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .shaderSampledImageArrayNonUniformIndexing = true,
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
    VkPhysicalDeviceFeatures enabled_features_10{.samplerAnisotropy = VK_TRUE};

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

    check(vkCreateDevice(devices[device_index], &device_create_info, nullptr, &device));
    vkGetDeviceQueue(device, queue_family_index, 0, &queue);
    std::printf("Logical device and queue created.\n");

    // VMA
    VmaVulkanFunctions vk_functions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkCreateImage = vkCreateImage
    };
    VmaAllocatorCreateInfo allocator_create_info{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = devices[device_index],
        .device = device,
        .pVulkanFunctions = &vk_functions,
        .instance = instance
    };
    check(vmaCreateAllocator(&allocator_create_info, &allocator));
    std::printf("Allocator created.\n");

    // window and surface
    SDL_Window *window = SDL_CreateWindow("Model Viewer", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    assert(window && "Failed to create window");
    check(SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface));
    check(SDL_GetWindowSize(window, &window_size.x, &window_size.y));
    VkSurfaceCapabilitiesKHR surface_capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[device_index], surface, &surface_capabilities));
    VkExtent2D swap_chain_extent{surface_capabilities.currentExtent};
    if (surface_capabilities.currentExtent.width == 0xFFFFFFFF) {
        swap_chain_extent = {
            .width = static_cast<uint32_t>(window_size.x), .height = static_cast<uint32_t>(window_size.y)
        };
    }
    std::printf("Window and surface created.\n");

    // swap-chain
    constexpr VkFormat image_format{VK_FORMAT_R8G8B8A8_SRGB};
    VkSwapchainCreateInfoKHR swap_chain_create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = surface_capabilities.minImageCount,
        .imageFormat = image_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent{.width = swap_chain_extent.width, .height = swap_chain_extent.height},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR
    };
    check(vkCreateSwapchainKHR(device, &swap_chain_create_info, nullptr, &swap_chain));
    uint32_t image_count{0};
    check(vkGetSwapchainImagesKHR(device, swap_chain, &image_count, nullptr));
    swap_chain_images.resize(image_count);
    check(vkGetSwapchainImagesKHR(device, swap_chain, &image_count, swap_chain_images.data()));
    swap_chain_image_views.resize(image_count);
    for (auto i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_view_create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swap_chain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = image_format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
        };
        check(vkCreateImageView(device, &image_view_create_info, nullptr, &swap_chain_image_views[i]));
    }
    std::printf("Swap chain images created.\n");

    // depth-attachment


    return EXIT_SUCCESS;
}
