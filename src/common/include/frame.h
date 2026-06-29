#pragma once

#include <vector>

#include <vulkan/vulkan.h>

struct FrameData {
    uint32_t max_frames_in_flight_{2};
    uint32_t frame_index_{0};
    uint32_t image_index_{0};
    // size of max frames in flight
    std::vector<VkCommandBuffer> command_buffers_;
    // per frame in flight
    std::vector<VkFence> fences_;
    // per frame in flight
    std::vector<VkSemaphore> image_acquired_semaphores_;
    // per swap chain image
    std::vector<VkSemaphore> render_complete_semaphores_;
    // per frame in flight
    std::vector<VkCommandBuffer> in_flight_command_buffers_;

    FrameData() {
        command_buffers_.resize(max_frames_in_flight_);
        fences_.resize(max_frames_in_flight_);
        image_acquired_semaphores_.resize(max_frames_in_flight_);
    }
};
