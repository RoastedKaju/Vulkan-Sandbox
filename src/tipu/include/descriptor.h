#pragma once

#include <queue>

#include <vulkan/vulkan.h>

class DescriptorRegistry {
public:
    static constexpr uint32_t kMaxTextureCount = 1024;

    DescriptorRegistry() = default;

    void init(VkDevice device);

    void destroy() const;

    uint32_t register_texture(VkImageView view, VkSampler sampler);

    void free_texture(uint32_t index);

    VkDescriptorPool get_pool() const { return pool_; }
    VkDescriptorSetLayout get_layout() const { return layout_; }
    VkDescriptorSet get_set() const { return set_; };

private:
    void create_pool();
    void create_layout();
    void allocate_descriptor_set();

    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorPool pool_{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
    VkDescriptorSet set_{VK_NULL_HANDLE};

    // index tracking
    std::queue<uint32_t> free_indices_;
    uint32_t next_index_{0};
};
