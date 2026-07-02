#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vma/vk_mem_alloc.h>

#include "frame.h"
#include "swap_chain.h"
#include "attachment.h"
#include "descriptor.h"

struct Config {
    std::string app_name_ = "default";
    VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    bool enable_validation_ = true;
};

struct TextureDesc {
    glm::ivec2 dimension_{0, 0};
    uint32_t depth_{1};
    uint32_t mip_levels_{1};
    uint32_t array_layers_{1};
    VkSampleCountFlagBits samples_{VK_SAMPLE_COUNT_1_BIT};
    VkImageTiling tiling_{VK_IMAGE_TILING_OPTIMAL};
    VkImageUsageFlags usage_{0};
    VkImageAspectFlags aspect_{VK_IMAGE_ASPECT_COLOR_BIT};
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    bool prefer_dedicated_alloc_ = false;
};

class Context {
public:
    explicit Context(const Config &config);

    Context(const Context &) = delete;

    Context(Context &&) = delete;

    Context &operator=(const Context &) = delete;

    Context &operator=(Context &&) = delete;

    ~Context();

    bool initialize();

    SDL_Window *create_window(const char *title, uint32_t width, uint32_t height);

    std::unique_ptr<Image> create_texture(const TextureDesc &desc) const;

    VkFormat get_device_depth_format() const;

    void acquire_command_buffer();

    void begin_rendering(const Attachment &attachment, const FrameBuffer &frame_buffer);

    void bind_pipeline(VkPipeline pipeline) const;

    void bind_descriptor_set(VkPipelineLayout pipeline_layout, VkDescriptorSet descriptor_set) const;

    void bind_vertex_buffer(VkBuffer buffer) const;

    void bind_index_buffer(VkBuffer buffer) const;

    void cmd_push_constants(VkPipelineLayout pipeline_layout, VkDeviceAddress address) const;

    void draw_indexed(uint32_t index_count) const;

    void draw(uint32_t vertex_count) const;

    void end_rendering() const;

    void submit();

    std::unique_ptr<Image> load_texture(const std::filesystem::path &path);

    /**
     *
     * @return number of maximum frames in flight
     */
    VkDevice get_device() const { return device_; }
    VkPhysicalDevice get_physical_device() const { return physical_device_; }
    SwapChain &get_swap_chain() { return swap_chain_; }
    uint32_t get_max_frame_count() const { return frame_data_.max_frames_in_flight_; }
    uint32_t get_frame_index() const { return frame_data_.frame_index_; }
    uint32_t get_image_index() const { return frame_data_.image_index_; }
    DescriptorRegistry &get_texture_registry() { return descriptor_registry_; }

private:
    bool create_instance(const char *app_name = "default");

    bool setup_device(uint32_t device_index = 0);

    void create_swap_chain();

    void create_frame_resources();

    static void transition_image(VkCommandBuffer cmd,
                                 VkImage image,
                                 ImageState &state,
                                 VkImageLayout new_layout,
                                 VkAccessFlags2 new_access,
                                 VkPipelineStageFlags2 new_stage,
                                 VkImageAspectFlags aspect,
                                 uint32_t level_count = 1,
                                 uint32_t layer_count = 1,
                                 uint32_t base_mip_level = 0);

    void create_default_sampler();

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

    // bindless texture descriptors
    DescriptorRegistry descriptor_registry_;

    // pools
    VkCommandPool command_pool_{VK_NULL_HANDLE};

    // frame data
    FrameData frame_data_;

    VkSampler default_sampler_{VK_NULL_HANDLE};

    friend class SwapChain;
    friend class Buffer;
    friend class Shader;
    friend class PipelineLayoutBuilder;
    friend class PipelineBuilder;
};
