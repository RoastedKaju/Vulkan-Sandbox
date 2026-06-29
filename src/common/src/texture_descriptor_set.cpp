#include "texture_descriptor_set.h"

#include <volk.h>

#include "utils.h"

void TextureDescriptorSet::create(const VkDevice device, const uint32_t texture_count) {
    max_texture_count_ = texture_count;

    VkDescriptorBindingFlags desc_binding_flags{
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 1,
        .pBindingFlags = &desc_binding_flags
    };

    VkDescriptorSetLayoutBinding texture_binding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = max_texture_count_,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    const VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &binding_flags_create_info,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings = &texture_binding
    };

    check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout_));

    VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = max_texture_count_
    };

    const VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size
    };

    check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool_));

    uint32_t variable_descriptor_count = max_texture_count_;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variable_descriptor_count
    };

    const VkDescriptorSetAllocateInfo desc_set_allocate_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = &variable_count_info,
        .descriptorPool = descriptor_pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptor_set_layout_
    };

    check(vkAllocateDescriptorSets(device, &desc_set_allocate_info, &descriptor_set_));
}

void TextureDescriptorSet::update_texture_slot(const VkDevice device,
                                               const uint32_t slot,
                                               const VkDescriptorImageInfo &image_info) const {
    assert(slot < max_texture_count_ && "Slot out of range.");
    const VkWriteDescriptorSet write_desc_set{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptor_set_,
        .dstBinding = 0,
        .dstArrayElement = slot, // Update the specific index in our giant array
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info
    };

    vkUpdateDescriptorSets(device, 1, &write_desc_set, 0, nullptr);
}
