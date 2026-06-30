#pragma once

#include <vector>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "context.h"

class Context;

class Buffer {
public:
    Buffer() = default;

    void create_buffer(const Context *context, VkDeviceSize size, VkBufferUsageFlags usage);

    void update(const void *src) const;

    VkBuffer get() const { return buffer_; }

    VkDeviceAddress device_address(const Context *context) const;

private:
    VkBuffer buffer_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VmaAllocationInfo allocation_info_{};
    VkDeviceSize size_{0};
};

template<typename T>
class PerFrameBuffer {
public:
    explicit PerFrameBuffer(const Context *context) : ctx_(context),
                                                      buffers_(context->get_max_frame_count()) {
        for (auto &buffer: buffers_) {
            buffer.create_buffer(context, sizeof(T), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        }
    }

    void update(const uint32_t frame, const T *src) const {
        buffers_[frame].update(src);
    }

    VkDeviceAddress address(const uint32_t frame) const { return buffers_[frame].device_address(ctx_); }

public:
    const Context *ctx_;
    std::vector<Buffer> buffers_;
};
