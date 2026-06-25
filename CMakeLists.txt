cmake_minimum_required(VERSION 4.0)

# find vulkan
if (NOT IS_DIRECTORY $ENV{VULKAN_SDK})
    message(WARNING "Vulkan SDK not found.")
else ()
    message(STATUS "Vulkan SDK found: $ENV{VULKAN_SDK}")
endif ()

project(VulkanSandbox)

find_package(Vulkan REQUIRED)

add_subdirectory(extern/glm)
add_subdirectory(extern/sdl3)
add_subdirectory(extern/vma)
add_subdirectory(extern/volk)

add_subdirectory(src/model_viewer)
