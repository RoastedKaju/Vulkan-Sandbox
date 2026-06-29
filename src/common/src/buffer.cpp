#include "buffer.h"

#include <volk.h>

#include "context.h"
#include "utils.h"

void Buffer::create_buffer(const Context *context, const VkDeviceSize size, const VkBufferUsageFlags usage) {
    size_ = size;

    const VkBufferCreateInfo buffer_create_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size_,
        .usage = usage
    };
    constexpr VmaAllocationCreateInfo buffer_allocation_create_info{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    check(vmaCreateBuffer(
        context->allocator_,
        &buffer_create_info,
        &buffer_allocation_create_info,
        &buffer_,
        &allocation_,
        &allocation_info_));
}

void Buffer::copy_data(const void *src) const {
    memcpy(allocation_info_.pMappedData, src, size_);
}

VkDeviceAddress Buffer::device_address(const Context *context) const {
    const VkBufferDeviceAddressInfo uniform_buffer_device_address_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer_,
    };
    return vkGetBufferDeviceAddress(context->device_, &uniform_buffer_device_address_info);
}
