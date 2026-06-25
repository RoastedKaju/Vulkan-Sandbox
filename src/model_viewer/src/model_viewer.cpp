#include <iostream>
#include <vector>
#include <array>
#include <cassert>
#include <unordered_map>
#include <filesystem>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#define VOLK_IMPLEMENTATION
#include <volk.h>
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

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

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

static const uint8_t *get_accessor_data(const tinygltf::Model &model, const tinygltf::Accessor &accessor) {
    const auto &view = model.bufferViews[accessor.bufferView];
    const auto &buffer = model.buffers[view.buffer];

    return buffer.data.data() + view.byteOffset + accessor.byteOffset;
}

static MeshData load_mesh_data(const tinygltf::Model &model, const tinygltf::Primitive &primitive) {
    MeshData mesh;

    const auto pos_it = primitive.attributes.find("POSITION");
    if (pos_it == primitive.attributes.end()) {
        throw std::runtime_error("Primitive has no POSITION attribute.");
    }

    const auto norm_it = primitive.attributes.find("NORMAL");
    const auto uv_it = primitive.attributes.find("TEXCOORD_0");

    const auto &pos_accessor = model.accessors[pos_it->second];

    const auto *positions = reinterpret_cast<const float *>(get_accessor_data(model, pos_accessor));

    const float *normals = nullptr;
    const float *uvs = nullptr;

    if (norm_it != primitive.attributes.end()) {
        normals = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[norm_it->second]));
    }
    if (uv_it != primitive.attributes.end()) {
        uvs = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[uv_it->second]));
    }

    mesh.vertices.resize(pos_accessor.count);

    for (size_t i = 0; i < pos_accessor.count; i++) {
        Vertex v{};

        v.position = {
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]
        };

        if (normals) {
            v.normal = {
                normals[i * 3 + 0],
                normals[i * 3 + 1],
                normals[i * 3 + 2],
            };
        }

        if (uvs) {
            v.uv = {
                uvs[i * 2 + 0],
                uvs[i * 2 + 1],
            };
        }

        mesh.vertices[i] = v;
    }

    if (primitive.indices >= 0) {
        const auto &index_accessor = model.accessors[primitive.indices];

        const uint8_t *index_data = get_accessor_data(model, index_accessor);

        mesh.indices.resize(index_accessor.count);

        switch (index_accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const auto *src = reinterpret_cast<const uint8_t *>(index_data);
                for (auto i = 0; i < index_accessor.count; i++) {
                    mesh.indices[i] = src[i];
                }
                break;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const auto *src = reinterpret_cast<const uint16_t *>(index_data);
                for (auto i = 0; i < index_accessor.count; i++) {
                    mesh.indices[i] = src[i];
                }
                break;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                const auto *src = reinterpret_cast<const uint32_t *>(index_data);
                for (auto i = 0; i < index_accessor.count; i++) {
                    mesh.indices[i] = src[i];
                }
                break;
            }

            default:
                throw std::runtime_error("Unsupported index type.");
        }
    }

    return mesh;
}

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
    std::vector<VkFormat> depth_format_list{VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    VkFormat depth_format{VK_FORMAT_D32_SFLOAT};
    for (VkFormat &format: depth_format_list) {
        VkFormatProperties2 format_properties{.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};
        vkGetPhysicalDeviceFormatProperties2(devices[device_index], format, &format_properties);
        if (format_properties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depth_format = format;
            break;
        }
    }

    assert(depth_format != VK_FORMAT_UNDEFINED && "Depth format is undefined.\n");
    VkImageCreateInfo depth_image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depth_format,
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
    VmaAllocationCreateInfo allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    check(vmaCreateImage(
        allocator,
        &depth_image_create_info,
        &allocation_create_info,
        &depth_image,
        &depth_image_allocation, nullptr));
    VkImageViewCreateInfo depth_view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = depth_format,
        .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
    };
    check(vkCreateImageView(device, &depth_view_create_info, nullptr, &depth_image_view));
    std::printf("Depth image created.\n");

    // mesh
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::string warnings{};
    std::string errors{};

    const std::filesystem::path model_path{"cube.glb"};

    if (!std::filesystem::exists(model_path)) {
        std::printf("Model path is invalid.\n");
        exit(EXIT_FAILURE);
    }

    if (model_path.extension() == ".glb") {
        check(loader.LoadBinaryFromFile(&model, &errors, &warnings, model_path.string()));
    } else {
        check(loader.LoadASCIIFromFile(&model, &errors, &warnings, model_path.string()));
    }

    if (!warnings.empty()) {
        std::printf("Warning: %s\n", warnings.c_str());
    }
    if (!errors.empty()) {
        std::printf("Errors: %s\n", errors.c_str());
    }

    tinygltf::Mesh &mesh = model.meshes[0];
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto &primitive: mesh.primitives) {
        MeshData mesh_data = load_mesh_data(model, primitive);

        auto vertex_offset = static_cast<uint32_t>(vertices.size());

        vertices.insert(vertices.end(), mesh_data.vertices.begin(), mesh_data.vertices.end());

        for (uint32_t index: mesh_data.indices) {
            indices.push_back(index + vertex_offset);
        }
    }

    VkDeviceSize vertex_buf_size = sizeof(Vertex) * vertices.size();
    VkDeviceSize index_buf_size = sizeof(uint32_t) * indices.size();

    VkBufferCreateInfo buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = vertex_buf_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    };
    VmaAllocationCreateInfo vertex_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    VmaAllocationInfo vertex_buffer_allocation_info{};
    check(vmaCreateBuffer(
        allocator,
        &buffer_create_info,
        &vertex_allocation_create_info,
        &vertex_buffer,
        &vertex_buffer_allocation,
        &vertex_buffer_allocation_info));
    memcpy(vertex_buffer_allocation_info.pMappedData, vertices.data(), vertex_buf_size);
    memcpy(static_cast<char *>(vertex_buffer_allocation_info.pMappedData) + vertex_buf_size,
           indices.data(),
           index_buf_size);
    std::printf("Mesh data loaded.\n");

    return EXIT_SUCCESS;
}
