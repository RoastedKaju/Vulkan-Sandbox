#pragma once

#include <filesystem>
#include <fstream>

#include <tiny_gltf.h>
#include <stb_image.h>

#include "context.h"
#include "mesh.h"


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
    MeshData mesh;

    const auto pos_it = primitive.attributes.find("POSITION");
    if (pos_it == primitive.attributes.end()) {
        throw std::runtime_error("Primitive has no POSITION attribute.");
    }

    const auto norm_it = primitive.attributes.find("NORMAL");
    const auto uv_it = primitive.attributes.find("TEXCOORD_0");
    const auto tangent_it = primitive.attributes.find("TANGENT");
    if (tangent_it == primitive.attributes.end()) {
        std::cout << "Mesh has no tangents!\n";
    }

    const auto &pos_accessor = model.accessors[pos_it->second];

    const auto *positions = reinterpret_cast<const float *>(get_accessor_data(model, pos_accessor));

    const float *normals = nullptr;
    const float *uvs = nullptr;
    const float *tangents = nullptr;

    if (tangent_it == primitive.attributes.end()) {
        std::cout << "Mesh has no tangents!\n";
    }

    if (norm_it != primitive.attributes.end()) {
        normals = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[norm_it->second]));
    }

    if (uv_it != primitive.attributes.end()) {
        uvs = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[uv_it->second]));
    }

    if (tangent_it != primitive.attributes.end()) {
        tangents = reinterpret_cast<const float *>(get_accessor_data(model, model.accessors[tangent_it->second]));
    }

    mesh.vertices_.resize(pos_accessor.count);

    for (size_t i = 0; i < pos_accessor.count; ++i) {
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
                normals[i * 3 + 2]
            };
        }

        if (uvs) {
            v.uv_ = {
                uvs[i * 2 + 0],
                uvs[i * 2 + 1]
            };
        }

        if (tangents) {
            v.tangent_ = {
                tangents[i * 4 + 0],
                tangents[i * 4 + 1],
                tangents[i * 4 + 2],
                tangents[i * 4 + 3]
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
                for (size_t i = 0; i < index_accessor.count; ++i)
                    mesh.indices_[i] = src[i];
                break;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                const auto *src = reinterpret_cast<const uint16_t *>(index_data);
                for (size_t i = 0; i < index_accessor.count; ++i)
                    mesh.indices_[i] = src[i];
                break;
            }

            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                const auto *src = reinterpret_cast<const uint32_t *>(index_data);
                for (size_t i = 0; i < index_accessor.count; ++i)
                    mesh.indices_[i] = src[i];
                break;
            }

            default:
                throw std::runtime_error("Unsupported index type.");
        }
    }

    return mesh;
}
