add_executable(ModelViewer)

target_compile_features(ModelViewer PRIVATE cxx_std_20)

file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")

target_sources(ModelViewer PRIVATE ${SRC_FILES} ${HED_FILES})