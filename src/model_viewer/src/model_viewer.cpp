#include <iostream>
#include <vector>
#include <array>
#include <cassert>
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
#include <shaderc/shaderc.hpp>

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

constexpr uint32_t kMaxTextures = 64;

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

VmaAllocation index_buffer_allocation{VK_NULL_HANDLE};
VkBuffer index_buffer{VK_NULL_HANDLE};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct ShaderData {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
};

ShaderData shader_data{};

struct ShaderDataBuffer {
    VmaAllocation allocation{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info{};
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceAddress device_address{};
};

std::array<ShaderDataBuffer, kMaxFramesInFlight> shader_data_buffers{};

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

static std::string read_text_file(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File does not exist.");
    }

    auto &&stream = std::ifstream(path, std::ios::binary);

    stream.seekg(0, std::ios::end);
    const size_t length = stream.tellg();
    stream.seekg(0, std::ios::beg);

    auto &&result = std::string(length, '\0');
    stream.read(result.data(), length);

    return result;
}

static std::vector<uint32_t> compile_shader(const std::filesystem::path &path, const shaderc_shader_kind kind) {
    const std::string source{read_text_file(path)};

    const shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

    const auto result{compiler.CompileGlslToSpv(source, kind, path.string().c_str(), options)};

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error(result.GetErrorMessage());
    }

    return {result.cbegin(), result.cend()};
}

static VkShaderModule create_shader_module(const std::vector<uint32_t> &spirv) {
    const VkShaderModuleCreateInfo shader_module_create_info{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data()
    };

    VkShaderModule shader_module{VK_NULL_HANDLE};

    // ReSharper disable once CppTooWideScopeInitStatement
    const auto result = vkCreateShaderModule(device, &shader_module_create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module.");
    }

    return shader_module;
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
    SDL_Window *window = SDL_CreateWindow("Model Viewer", 1280u, 720u, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
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
    VmaAllocationCreateInfo depth_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    check(vmaCreateImage(
        allocator,
        &depth_image_create_info,
        &depth_allocation_create_info,
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

    const std::filesystem::path model_path{"tank.glb"};

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
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    };
    VmaAllocationCreateInfo buffer_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    VmaAllocationInfo vertex_buffer_allocation_info{};
    check(vmaCreateBuffer(
        allocator,
        &buffer_create_info,
        &buffer_allocation_create_info,
        &vertex_buffer,
        &vertex_buffer_allocation,
        &vertex_buffer_allocation_info));
    memcpy(vertex_buffer_allocation_info.pMappedData, vertices.data(), vertex_buf_size);
    // Index buffer
    VkBufferCreateInfo index_buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = index_buf_size,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    };
    VmaAllocationInfo index_buffer_allocation_info{};
    check(vmaCreateBuffer(
        allocator,
        &index_buffer_create_info,
        &buffer_allocation_create_info,
        &index_buffer,
        &index_buffer_allocation,
        &index_buffer_allocation_info));
    memcpy(index_buffer_allocation_info.pMappedData, indices.data(), index_buf_size);

    std::printf("Mesh data loaded.\n");

    // shader data buffer
    for (auto i = 0; i < kMaxFramesInFlight; ++i) {
        VkBufferCreateInfo uniform_buffer_create_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(ShaderData),
            .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        };
        VmaAllocationCreateInfo uniform_buffer_alloc_create_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        check(vmaCreateBuffer(
            allocator,
            &uniform_buffer_create_info, &uniform_buffer_alloc_create_info,
            &shader_data_buffers[i].buffer, &shader_data_buffers[i].allocation,
            &shader_data_buffers[i].allocation_info));
        VkBufferDeviceAddressInfo uniform_buffer_device_address_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = shader_data_buffers[i].buffer,
        };
        shader_data_buffers[i].device_address = vkGetBufferDeviceAddress(device, &uniform_buffer_device_address_info);
    }
    std::printf("Shader data buffers created.\n");

    // sync-objects
    VkSemaphoreCreateInfo semaphore_create_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    for (auto i = 0; i < kMaxFramesInFlight; ++i) {
        check(vkCreateFence(device, &fence_create_info, nullptr, &fences[i]));
        check(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &image_acquired_semaphores[i]));
    }
    render_complete_semaphores.resize(swap_chain_images.size());
    for (auto &semaphore: render_complete_semaphores) {
        check(vkCreateSemaphore(device, &semaphore_create_info, nullptr, &semaphore));
    }
    std::printf("Sync objects created.\n");

    // command pool
    VkCommandPoolCreateInfo command_pool_create_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index
    };
    check(vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool));
    VkCommandBufferAllocateInfo command_buffer_allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = command_pool,
        .commandBufferCount = kMaxFramesInFlight
    };
    check(vkAllocateCommandBuffers(device, &command_buffer_allocate_info, command_buffers.data()));
    std::printf("Command pool and buffers created.\n");

    // descriptors
    std::vector<VkDescriptorImageInfo> texture_descriptors{};
    VkDescriptorPool descriptor_pool{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set{VK_NULL_HANDLE};

    VkDescriptorBindingFlags desc_binding_flags{
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 1,
        .pBindingFlags = &desc_binding_flags
    };

    VkDescriptorSetLayoutBinding texture_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kMaxTextures,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &binding_flags_create_info,
        .bindingCount = 1,
        .pBindings = &texture_binding
    };

    check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout));

    VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kMaxTextures
    };

    VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size
    };

    check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool));

    uint32_t variable_descriptor_count = kMaxTextures;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variable_descriptor_count
    };

    VkDescriptorSetAllocateInfo desc_set_allocate_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = &variable_count_info,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_set_layout
    };

    check(vkAllocateDescriptorSets(device, &desc_set_allocate_info, &descriptor_set));

    if (!texture_descriptors.empty()) {
        VkWriteDescriptorSet write_desc_set{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptor_set,
            .dstBinding = 0,
            .descriptorCount = static_cast<uint32_t>(texture_descriptors.size()),
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = texture_descriptors.data()
        };

        vkUpdateDescriptorSets(device, 1, &write_desc_set, 0, nullptr);
    }
    std::printf("Descriptor sets created.\n");

    // load shaders
    auto vertex_spirv{compile_shader("vert.glsl", shaderc_vertex_shader)};
    auto frag_spirv{compile_shader("frag.glsl", shaderc_fragment_shader)};

    [[maybe_unused]] VkShaderModule vertex_shader_module{create_shader_module(vertex_spirv)};
    [[maybe_unused]] VkShaderModule fragment_shader_module{create_shader_module(frag_spirv)};
    std::printf("Shader modules created.\n");

    // pipeline
    VkPushConstantRange push_constant_range{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(VkDeviceAddress)
    };
    VkPipelineLayoutCreateInfo layout_create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range
    };
    check(vkCreatePipelineLayout(device, &layout_create_info, nullptr, &pipeline_layout));
    VkPipelineShaderStageCreateInfo shader_stages[]{
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertex_shader_module,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragment_shader_module,
            .pName = "main",
        }
    };
    VkVertexInputBindingDescription vertex_binding{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    std::vector<VkVertexInputAttributeDescription> vertex_attributes{
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo vertex_input_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attributes.size()),
        .pVertexAttributeDescriptions = vertex_attributes.data()
    };
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    std::vector<VkDynamicState> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states.data()
    };
    VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo rasterization_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkPipelineColorBlendAttachmentState blend_attachment{.colorWriteMask = 0xF};
    VkPipelineColorBlendStateCreateInfo color_blend_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend_attachment
    };
    VkPipelineRenderingCreateInfo rendering_create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &image_format,
        .depthAttachmentFormat = depth_format
    };
    VkGraphicsPipelineCreateInfo pipeline_create_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_create_info,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState = &multisample_state,
        .pDepthStencilState = &depth_stencil_state,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = &dynamic_state,
        .layout = pipeline_layout
    };
    check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &pipeline));
    std::printf("Pipeline created.\n");

    // render loop
    uint64_t last_time{SDL_GetTicks()};
    bool quit{false};
    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    quit = true;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                    is_swap_chain_dirty = true;
                    break;
                default:
                    break;
            }
        }

        [[maybe_unused]] float elapsed_time{(SDL_GetTicks() - last_time) / 1000.0f};
        last_time = SDL_GetTicks();

        // sync
        check(vkWaitForFences(device, 1, &fences[frame_index], true, UINT64_MAX));
        check(vkResetFences(device, 1, &fences[frame_index]));
        check_swap_chain(vkAcquireNextImageKHR(device,
                                               swap_chain,
                                               UINT64_MAX,
                                               image_acquired_semaphores[frame_index],
                                               VK_NULL_HANDLE, &image_index));

        // update shader data
        shader_data.projection = glm::perspective(glm::radians(45.0f),
                                                  static_cast<float>(window_size.x) / static_cast<float>(window_size.y),
                                                  0.1f, 1000.0f);
        shader_data.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
        auto time = SDL_GetTicks() / 1000.0f;
        auto m = glm::mat4(1.0f);
        m = glm::translate(m, glm::vec3(0.0f, 0.0f, 0.0f));
        m = glm::rotate(m, glm::radians(45.0f * time), glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(0.3f, 0.3f, 0.3f));
        shader_data.model = m;
        memcpy(shader_data_buffers[frame_index].allocation_info.pMappedData, &shader_data, sizeof(shader_data));

        // record commands
        auto cmd = command_buffers[frame_index];
        check(vkResetCommandBuffer(cmd, 0));
        VkCommandBufferBeginInfo cmd_begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        check(vkBeginCommandBuffer(cmd, &cmd_begin_info));
        // barriers
        std::array<VkImageMemoryBarrier2, 2> output_barriers{
            VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = swap_chain_images[image_index],
                .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
            },
            VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = depth_image,
                .subresourceRange{
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
                    .levelCount = 1,
                    .layerCount = 1
                }
            }
        };
        VkDependencyInfo barrier_dep_info{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = output_barriers.data()
        };
        vkCmdPipelineBarrier2(cmd, &barrier_dep_info);
        // rendering attachments
        VkRenderingAttachmentInfo color_attachment_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swap_chain_image_views[image_index],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {0.0f, 0.0f, 0.0f, 1.0f}}
        };
        VkRenderingAttachmentInfo depth_attachment_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth_image_view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {1.0f, 0}}
        };
        VkRenderingInfo rendering_info{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea{
                .extent{.width = static_cast<uint32_t>(window_size.x), .height = static_cast<uint32_t>(window_size.y)}
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment_info,
            .pDepthAttachment = &depth_attachment_info,
        };

        // dynamic rendering
        vkCmdBeginRendering(cmd, &rendering_info);
        VkViewport viewport{
            .width = static_cast<float>(window_size.x),
            .height = static_cast<float>(window_size.y),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{
            .extent{.width = static_cast<uint32_t>(window_size.x), .height = static_cast<uint32_t>(window_size.y)}
        };
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0,
                                nullptr);
        VkDeviceSize vertex_offset{0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdPushConstants(cmd,
                           pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(VkDeviceAddress),
                           &shader_data_buffers[frame_index].device_address);

        // draw
        vkCmdDrawIndexed(cmd, indices.size(), 1, 0, 0, 0);
        vkCmdEndRendering(cmd);

        // present
        VkImageMemoryBarrier2 barrier_present{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = swap_chain_images[image_index],
            .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
        };
        VkDependencyInfo barrier_present_dependency_info{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier_present
        };
        vkCmdPipelineBarrier2(cmd, &barrier_present_dependency_info);
        check(vkEndCommandBuffer(cmd));

        // submit
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &image_acquired_semaphores[frame_index],
            .pWaitDstStageMask = &wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &render_complete_semaphores[image_index]
        };
        check(vkQueueSubmit(queue, 1, &submit_info, fences[frame_index]));
        frame_index = (frame_index + 1) % kMaxFramesInFlight;
        VkPresentInfoKHR present_info{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &render_complete_semaphores[image_index],
            .swapchainCount = 1,
            .pSwapchains = &swap_chain,
            .pImageIndices = &image_index
        };
        check_swap_chain(vkQueuePresentKHR(queue, &present_info));
    }

    return EXIT_SUCCESS;
}
