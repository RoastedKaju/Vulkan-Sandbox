#pragma once

#include <vector>

#include <vulkan/vulkan.h>

class TextureDescriptorSet {
public:
    void create(VkDevice device, const uint32_t texture_count);

    void update_texture_slot(const VkDevice device, uint32_t slot, const VkDescriptorImageInfo &image_info) const;

    VkDescriptorSet get() const { return descriptor_set_; }
    VkDescriptorSetLayout layout() const { return descriptor_set_layout_; }

private:
    uint32_t max_texture_count_{64};
    VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout_{VK_NULL_HANDLE};
    VkDescriptorSet descriptor_set_{VK_NULL_HANDLE};
};
