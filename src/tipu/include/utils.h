#pragma once

#include <array>
#include <filesystem>
#include <fstream>

#include <volk.h>
#include <tiny_gltf.h>
#include <stb_image.h>

#include "context.h"
#include "mesh.h"
#include "image.h"
#include "buffer.h"


inline void check(const VkResult result) {
    if (result != VK_SUCCESS) {
        printf("Vulkan call returned an error: %d\n", result);
        exit(EXIT_FAILURE);
    }
}

inline void check(const bool result) {
    if (!result) {
        printf("Call returned an error\n");
        exit(EXIT_FAILURE);
    }
}

inline void check_swap_chain(const VkResult result, bool &is_swap_chain_dirty) {
    if (result < VK_SUCCESS) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            is_swap_chain_dirty = true;
            return;
        }
        printf("Swap-chain check failed %d\b", result);
        exit(EXIT_FAILURE);
    }
}

inline std::string read_text_file(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("File does not exist " + path.string() + '.');
    }

    auto &&stream = std::ifstream(path, std::ios::binary);

    stream.seekg(0, std::ios::end);
    const size_t length = stream.tellg();
    stream.seekg(0, std::ios::beg);

    auto &&result = std::string(length, '\0');
    stream.read(result.data(), length);

    return result;
}

inline const uint8_t *get_accessor_data(const tinygltf::Model &model, const tinygltf::Accessor &accessor) {
    const auto &view = model.bufferViews[accessor.bufferView];
    const auto &buffer = model.buffers[view.buffer];

    return buffer.data.data() + view.byteOffset + accessor.byteOffset;
}

inline MeshData load_mesh_data(const tinygltf::Model &model, const tinygltf::Primitive &primitive) {
    // output mesh data
    MeshData mesh;

    const auto pos_it = primitive.attributes.find("POSITION");
    if (pos_it == primitive.attributes.end()) {
        throw std::runtime_error("Primitive has no POSITION attribute.");
    }

    const auto norm_it = primitive.attributes.find("NORMAL");
    const auto uv_it = primitive.attributes.find("TEXCOORD_0");

    const auto &pos_accessor = model.accessors[pos_it->second];

    const auto *positions = reinterpret_cast<const float *>(get_accessor_data(model, pos_accessor));

    const float *normals = nullptr;
    const float *uvs = nullptr;

    if (norm_it != primitive.attributes.end()) {
        normals = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[norm_it->second]));
    }
    if (uv_it != primitive.attributes.end()) {
        uvs = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[uv_it->second]));
    }

    mesh.vertices_.resize(pos_accessor.count);

    for (size_t i = 0; i < pos_accessor.count; i++) {
        Vertex v{};

        v.position_ = {
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]
        };

        if (normals) {
            v.normal_ = {
                normals[i * 3 + 0],
                normals[i * 3 + 1],
                normals[i * 3 + 2],
            };
        }

        if (uvs) {
            v.uv_ = {
                uvs[i * 2 + 0],
                uvs[i * 2 + 1],
            };
        }

        mesh.vertices_[i] = v;
    }

    if (primitive.indices >= 0) {
        const auto &index_accessor = model.accessors[primitive.indices];

        const uint8_t *index_data = get_accessor_data(model, index_accessor);

        mesh.indices_.resize(index_accessor.count);

        switch (index_accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
                const auto *src = reinterpret_cast<const uint8_t *>(index_data);
                for (auto i = 0; i < index_accessor.count; i++) {
                    mesh.indices_[i] = src[i];
                }
                break;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const auto *src = reinterpret_cast<const uint16_t *>(index_data);
                for (auto i = 0; i < index_accessor.count; i++) {
                    mesh.indices_[i] = src[i];
                }
                break;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                const auto *src = reinterpret_cast<const uint32_t *>(index_data);
                for (auto i = 0; i < index_accessor.count; i++) {
                    mesh.indices_[i] = src[i];
                }
                break;
            }

            default:
                throw std::runtime_error("Unsupported index type.");
        }
    }

    return mesh;
}

inline std::unique_ptr<Image> load_cubemap(const Context *context, const std::array<std::filesystem::path, 6> &paths) {
    // Expected face order: +X, -X, +Y, -Y, +Z, -Z (Vulkan cube array layer order)

    int width = 0, height = 0, channels = 0;
    std::array<unsigned char *, 6> face_pixels{};

    for (size_t i = 0; i < 6; ++i) {
        if (!std::filesystem::exists(paths[i])) {
            throw std::runtime_error("Cubemap face does not exist: " + paths[i].string());
        }

        int w = 0, h = 0, c = 0;
        unsigned char *pixels = stbi_load(paths[i].string().c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!pixels) {
            std::printf("Failed to load cubemap face: %s (%s)\n", paths[i].string().c_str(), stbi_failure_reason());
            exit(EXIT_FAILURE);
        }

        if (i == 0) {
            width = w;
            height = h;
        } else if (w != width || h != height) {
            throw std::runtime_error("Cubemap face size mismatch: " + paths[i].string());
        }

        face_pixels[i] = pixels;
    }

    // --- Create the cube-compatible image (6 array layers, one VkImage) ---
    auto image = std::make_unique<Image>();
    image->format_ = VK_FORMAT_R8G8B8A8_UNORM;
    image->aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    image->type_ = VK_IMAGE_TYPE_2D;
    image->usage_ = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image->width_ = static_cast<uint32_t>(width);
    image->height_ = static_cast<uint32_t>(height);
    image->depth_ = 1;
    image->mip_levels_ = 1;
    image->array_layers_ = 6;
    image->samples_ = VK_SAMPLE_COUNT_1_BIT;

    const VkImageCreateInfo image_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image->format_,
        .extent = {image->width_, image->height_, 1},
        .mipLevels = image->mip_levels_,
        .arrayLayers = image->array_layers_,
        .samples = image->samples_,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = image->usage_,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo alloc_create_info{};
    alloc_create_info.usage = VMA_MEMORY_USAGE_AUTO;

    check(vmaCreateImage(context->get_allocator(), &image_create_info, &alloc_create_info,
                         &image->image_, &image->allocation_, nullptr));

    const VkImageViewCreateInfo view_create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image->image_,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format = image->format_,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6,
        }
    };

    check(vkCreateImageView(context->get_device(), &view_create_info, nullptr, &image->view_));

    // --- Upload all 6 faces via one staging buffer ---
    const VkDeviceSize face_size = static_cast<VkDeviceSize>(width) * height * 4;
    const VkDeviceSize total_size = face_size * 6;

    const BufferDesc buf_desc{
        .context = context,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = total_size,
        .per_frame = false
    };
    Buffer data_buffer{};
    data_buffer.create(buf_desc);

    // Copy each face's pixels into its region of the staging buffer, then free CPU copy
    for (size_t i = 0; i < 6; ++i) {
        data_buffer.update(face_pixels[i], face_size, face_size * i);
        stbi_image_free(face_pixels[i]);
    }

    constexpr VkFenceCreateInfo fence_one_time_create_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence_one_time{};
    check(vkCreateFence(context->get_device(), &fence_one_time_create_info, nullptr, &fence_one_time));
    VkCommandBuffer cmd_buf_one_time{};
    const VkCommandBufferAllocateInfo cmd_buf_alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context->get_command_pool(),
        .commandBufferCount = 1
    };

    check(vkAllocateCommandBuffers(context->get_device(), &cmd_buf_alloc_info, &cmd_buf_one_time));
    constexpr VkCommandBufferBeginInfo cmd_buf_one_time_begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    check(vkBeginCommandBuffer(cmd_buf_one_time, &cmd_buf_one_time_begin));

    context->transition_image(cmd_buf_one_time,
                              *image,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT);

    // One copy region per face/layer; all six can be issued in a single vkCmdCopyBufferToImage call
    std::array<VkBufferImageCopy, 6> copy_regions{};
    for (uint32_t i = 0; i < 6; ++i) {
        copy_regions[i] = VkBufferImageCopy{
            .bufferOffset = face_size * i,
            .imageSubresource{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = i,
                .layerCount = 1
            },
            .imageExtent{.width = image->width_, .height = image->height_, .depth = 1}
        };
    }

    vkCmdCopyBufferToImage(
        cmd_buf_one_time,
        data_buffer.get(),
        image->image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(copy_regions.size()),
        copy_regions.data());

    context->transition_image(cmd_buf_one_time,
                              *image,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                              VK_ACCESS_2_SHADER_READ_BIT,
                              VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    check(vkEndCommandBuffer(cmd_buf_one_time));

    const VkSubmitInfo one_time_submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf_one_time
    };
    check(vkQueueSubmit(context->get_queue(), 1, &one_time_submit_info, fence_one_time));
    check(vkWaitForFences(context->get_device(), 1, &fence_one_time, VK_TRUE, UINT64_MAX));

    vkDestroyFence(context->get_device(), fence_one_time, nullptr);
    vkFreeCommandBuffers(context->get_device(), context->get_command_pool(), 1, &cmd_buf_one_time);
    vmaDestroyBuffer(context->get_allocator(), data_buffer.get(), data_buffer.get_allocation());

    // Register into the CUBE bindless array (separate binding/array from sampler2D)
    // image->bindless_index_ = descriptor_registry_.register_cubemap(image->view_, default_sampler_);

    return image;
}
