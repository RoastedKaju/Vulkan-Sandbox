add_library(Common INTERFACE)

target_link_libraries(Common
        INTERFACE Vulkan::Vulkan
        INTERFACE SDL3::SDL3
        INTERFACE volk
        INTERFACE GPUOpen::VulkanMemoryAllocator
        INTERFACE glm::glm
        INTERFACE TinyGLTF
)

# Volk define requirement
target_compile_definitions(Common INTERFACE VK_NO_PROTOTYPES)
# Shader-C linking
target_include_directories(Common INTERFACE "${CMAKE_SOURCE_DIR}/extern/shaderc")
target_link_directories(Common INTERFACE "${CMAKE_SOURCE_DIR}/extern/shaderc/bin")
target_link_libraries(Common INTERFACE shaderc_shared)