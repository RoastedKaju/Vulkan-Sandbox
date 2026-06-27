add_library(Common SHARED)

target_sources(Common PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/context.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/context.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/swap_chain.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/swap_chain.h"
)

target_include_directories(Common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(Common
        PUBLIC Vulkan::Vulkan
        PUBLIC SDL3::SDL3
        PUBLIC volk
        PUBLIC GPUOpen::VulkanMemoryAllocator
        PUBLIC glm::glm
        PUBLIC TinyGLTF
)

target_compile_definitions(Common PUBLIC VK_NO_PROTOTYPES)
target_compile_definitions(Common PRIVATE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Shader-C linking
target_include_directories(Common PUBLIC "${CMAKE_SOURCE_DIR}/extern/shaderc")
target_link_directories(Common PUBLIC "${CMAKE_SOURCE_DIR}/extern/shaderc/bin")
target_link_libraries(Common PUBLIC shaderc_shared)