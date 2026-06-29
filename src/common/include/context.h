#pragma once

#include <iostream>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vma/vk_mem_alloc.h>

#include "frame.h"
#include "swap_chain.h"
#include "texture_descriptor_set.h"

struct Config {
    std::string app_name_ = "default";
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    bool enable_validation_ = true;
    uint32_t max_texture_count_ = 128;
};

class Context {
public:
    explicit Context(const Config &config);

    ~Context();

    bool initialize();

    SDL_Window *create_window(const char *title, uint32_t width, uint32_t height);

    /**
     *
     * @return number of maximum frames in flight
     */
    uint32_t get_max_frame_count() const { return frame_data_.max_frames_in_flight_; }

private:
    bool create_instance(const char *app_name = "default");

    bool setup_device(uint32_t device_index = 0);

    void create_swap_chain();

    void create_frame_resources();

    Config config_;

    // validation
    const std::vector<const char *> validation_layers_{"VK_LAYER_KHRONOS_validation"};
    VkDebugUtilsMessengerEXT debug_messenger_{VK_NULL_HANDLE};

    // window
    SDL_Window *window_{nullptr};
    glm::ivec2 window_size_{};

    // device
    VkInstance instance_{VK_NULL_HANDLE};
    VkPhysicalDevice physical_device_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t queue_family_index_{0};
    VkSurfaceKHR surface_{VK_NULL_HANDLE};
    uint32_t device_index_{0};

    // allocator
    VmaAllocator allocator_{VK_NULL_HANDLE};

    // swap-chain
    SwapChain swap_chain_;

    // pools
    VkCommandPool command_pool_{VK_NULL_HANDLE};

    // frame data
    FrameData frame_data_;

    // texture descriptor set
    TextureDescriptorSet texture_descriptor_set_;

    friend class SwapChain;
    friend class Buffer;
    friend class Shader;
};
