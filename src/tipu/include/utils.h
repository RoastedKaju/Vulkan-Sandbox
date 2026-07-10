#pragma once

#include <filesystem>
#include <fstream>

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