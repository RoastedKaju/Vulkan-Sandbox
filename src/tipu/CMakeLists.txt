add_library(Tipu SHARED)

file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

target_sources(Tipu PRIVATE ${SRC_FILES} ${HED_FILES})

target_include_directories(Tipu PUBLIC "include")

target_link_libraries(Tipu
        PUBLIC SDL3::SDL3
        PUBLIC volk
        PUBLIC GPUOpen::VulkanMemoryAllocator
        PUBLIC glm::glm
        PUBLIC ImGUI
        PUBLIC STB
        PUBLIC assimp
)

target_compile_definitions(Tipu PUBLIC VK_NO_PROTOTYPES)
target_compile_definitions(Tipu PUBLIC GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Shader-C linking
target_link_libraries(Tipu PRIVATE shaderc_shared)