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
    } else {
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

uint32_t DescriptorRegistry::register_cubemap(const VkImageView view, const VkSampler sampler) {
    uint32_t index{};

    if (!free_cubemap_indices_.empty()) {
        index = free_cubemap_indices_.front();
        free_cubemap_indices_.pop();
    } else {
        if (next_cubemap_index_ >= kMaxCubemapCount) {
            throw std::runtime_error("Exceeded maximum bindless cubemap capacity!");
        }
        index = next_cubemap_index_++;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = view;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = set_;
    descriptorWrite.dstBinding = 1; // <-- cube binding
    descriptorWrite.dstArrayElement = index;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);

    return index;
}

void DescriptorRegistry::free_cubemap(uint32_t index) {
    free_cubemap_indices_.push(index);
}

void DescriptorRegistry::create_pool() {
    constexpr VkDescriptorPoolSize pool_size{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kMaxTextureCount + kMaxCubemapCount // 1024 + 256 = 1280
    };

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
    VkDescriptorBindingFlags flags_per_binding =
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    VkDescriptorBindingFlags binding_flags[2] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        flags_per_binding
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_create_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 2,
        .pBindingFlags = binding_flags
    };

    VkDescriptorSetLayoutBinding bindings[2]{
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = kMaxTextureCount,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = kMaxCubemapCount,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        }
    };

    const VkDescriptorSetLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &binding_flags_create_info,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = bindings
    };

    check(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &layout_));
}

void DescriptorRegistry::allocate_descriptor_set() {
    uint32_t variable_descriptor_count = kMaxCubemapCount;

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
