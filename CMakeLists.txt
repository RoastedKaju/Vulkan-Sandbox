cmake_minimum_required(VERSION 3.24)

project(VulkanSandbox)

# find vulkan
if (NOT IS_DIRECTORY $ENV{VULKAN_SDK})
    message(WARNING "Vulkan SDK not found.")
else ()
    message(STATUS "Vulkan SDK found: $ENV{VULKAN_SDK}")
endif ()

# set global CXX standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# unified output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

find_package(Vulkan REQUIRED)

# Third-party
add_subdirectory(extern/glm)
add_subdirectory(extern/sdl3)
add_subdirectory(extern/vma)
add_subdirectory(extern/volk)
add_subdirectory(extern/tinygltf)

# skip unnesscary build steps for shader-c
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)

add_subdirectory(extern/shaderc)

# Render library
add_subdirectory(src/tipu)

# Examples
add_subdirectory(examples/triangle)
add_subdirectory(examples/model)
add_subdirectory(examples/wireframe)
add_subdirectory(examples/cubemap)
