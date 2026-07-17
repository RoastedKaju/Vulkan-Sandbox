#pragma once

#include <filesystem>
#include <fstream>
#include <assimp/Importer.hpp>

#include "context.h"


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

inline glm::mat4 convert(const aiMatrix4x4 &from) {
    glm::mat4 to;
    to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
    to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
    to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
    to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
    return to;
}

inline glm::vec3 convert(const aiVector3D &v) {
    return {v.x, v.y, v.z};
}

inline glm::quat convert(const aiQuaternion &q) {
    return {q.w, q.x, q.y, q.z}; // glm::quat ctor is (w, x, y, z)
}
