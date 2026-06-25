cmake_minimum_required(VERSION 4.0)

# find vulkan
if (NOT IS_DIRECTORY $ENV{VULKAN_SDK})
    message(WARNING "Vulkan SDK not found.")
else ()
    message(STATUS "Vulkan SDK found: $ENV{VULKAN_SDK}")
endif ()

project(VulkanSandbox)

add_subdirectory(src/model_viewer)
