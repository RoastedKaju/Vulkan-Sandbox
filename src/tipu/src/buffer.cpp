#include "buffer.h"

#include <volk.h>

#include "context.h"
#include "utils.h"

void Buffer::create(const BufferDesc &desc) {
    desc_ = desc;

    const VkBufferCreateInfo buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = desc.usage_flags
    };
    constexpr VmaAllocationCreateInfo buffer_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };

    if (desc_.per_frame) {
        buffers_.resize(desc.context->get_max_frame_count());
    } else {
        buffers_.resize(1);
    }

    for (auto &buffer: buffers_) {
        buffer.size_ = desc.size;

        check(vmaCreateBuffer(
            desc.context->allocator_,
            &buffer_create_info,
            &buffer_allocation_create_info,
            &buffer.buffer_,
            &buffer.allocation_,
            &buffer.allocation_info_));
    }
}

void Buffer::update(const void *src) const {
    // if our buffer is dynamic (per frame) then update the correct one
    if (desc_.per_frame) {
        const uint32_t current_frame_index = desc_.context->get_frame_index();
        memcpy(buffers_[current_frame_index].allocation_info_.pMappedData, src, buffers_[current_frame_index].size_);
        return;
    }

    memcpy(buffers_[0].allocation_info_.pMappedData, src, buffers_[0].size_);
}

VkBuffer Buffer::get() const {
    if (desc_.per_frame) {
        const uint32_t current_frame_index = desc_.context->get_frame_index();
        return buffers_[current_frame_index].buffer_;
    }

    return buffers_[0].buffer_;
}

VmaAllocation Buffer::get_allocation() const {
    if (desc_.per_frame) {
        const uint32_t current_frame_index = desc_.context->get_frame_index();
        return buffers_[current_frame_index].allocation_;
    }

    return buffers_[0].allocation_;
}

VkDeviceAddress Buffer::address() const {
    if (desc_.per_frame) {
        const uint32_t current_frame_index = desc_.context->get_frame_index();
        const VkBufferDeviceAddressInfo uniform_buffer_device_address_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffers_[current_frame_index].buffer_,
        };
        return vkGetBufferDeviceAddress(desc_.context->device_, &uniform_buffer_device_address_info);
    }

    const VkBufferDeviceAddressInfo uniform_buffer_device_address_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffers_[0].buffer_,
    };
    return vkGetBufferDeviceAddress(desc_.context->device_, &uniform_buffer_device_address_info);
}
