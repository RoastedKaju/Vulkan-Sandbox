add_executable(ModelViewer)

target_compile_features(ModelViewer PRIVATE cxx_std_20)

file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")

target_sources(ModelViewer PRIVATE ${SRC_FILES} ${HED_FILES})

target_link_libraries(ModelViewer
        PRIVATE Vulkan::Vulkan
        PRIVATE SDL3::SDL3
        PRIVATE volk
        PRIVATE GPUOpen::VulkanMemoryAllocator
        PRIVATE glm::glm
        PRIVATE TinyGLTF)

target_compile_definitions(ModelViewer PRIVATE VK_NO_PROTOTYPES)

target_include_directories(ModelViewer SYSTEM PRIVATE "${CMAKE_SOURCE_DIR}/extern/shaderc")
target_link_directories(ModelViewer PRIVATE "${CMAKE_SOURCE_DIR}/extern/shaderc/bin")
target_link_libraries(ModelViewer PRIVATE shaderc_shared)