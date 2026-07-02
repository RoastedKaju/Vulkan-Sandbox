#include "descriptor.h"

#include <volk.h>

#include "utils.h"

void DescriptorRegistry::init(const VkDevice device) {
    device_ = device;

    create_pool();
    create_layout();
    allocate_descriptor_set();
}

void DescriptorRegistry::destroy() const {
    if (!device_) {
        return;
    }

    if (pool_) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
    }
    if (layout_) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
    }
}

uint32_t DescriptorRegistry::register_texture(const VkImageView view, const VkSampler sampler) {
    uint32_t index{};

    // check if free slot available
    if (!free_indices_.empty()) {
        index = free_indices_.front();
        free_indices_.pop();
    }
    else {
        if (next_index_ >= kMaxTextureCount) {
            throw std::runtime_error("Exceeded maximum bindless textures capacity!");
        }
        index = next_index_++;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = set_;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = index; // Target the specific slot in our bindless array
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);

    return index;
}

void DescriptorRegistry::free_texture(const uint32_t index) {
    free_indices_.push(index);
}

void DescriptorRegistry::create_pool() {
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = kMaxTextureCount;

    VkDescriptorPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size
    };

    check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &pool_));
}

void DescriptorRegistry::create_layout() {
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
        .descriptorCount = kMaxTextureCount,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };

    const VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &binding_flags_create_info,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 1,
        .pBindings = &texture_binding
    };

    check(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &layout_));
}

void DescriptorRegistry::allocate_descriptor_set() {
    uint32_t variable_descriptor_count = kMaxTextureCount;

    VkDescriptorSetVariableDescriptorCountAllocateInfo variable_count_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variable_descriptor_count
    };

    const VkDescriptorSetAllocateInfo desc_set_allocate_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = &variable_count_info,
        .descriptorPool = pool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout_
    };

    check(vkAllocateDescriptorSets(device_, &desc_set_allocate_info, &set_));
}


