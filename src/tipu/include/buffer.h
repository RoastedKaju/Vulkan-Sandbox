#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "context.h"

class Context;

struct BufferDesc {
    const Context *context;
    VkBufferUsageFlags usage_flags;
    size_t size{0};
    bool per_frame{false};
};

class Buffer {
public:
    Buffer() = default;

    void create(const BufferDesc &desc);

    void update(const void *src) const;

    void update(const void *src, VkDeviceSize size, VkDeviceSize offset) const;

    void destroy() const;

    // Getter for raw Vulkan buffer
    VkBuffer get() const;

    VmaAllocation get_allocation() const;

    // Getter for device address
    VkDeviceAddress address() const;

private:
    struct BufferData {
        VkBuffer buffer_{VK_NULL_HANDLE};
        VmaAllocation allocation_{VK_NULL_HANDLE};
        VmaAllocationInfo allocation_info_{};
        VkDeviceSize size_{0};
    };

    BufferDesc desc_;
    std::vector<BufferData> buffers_;
};
