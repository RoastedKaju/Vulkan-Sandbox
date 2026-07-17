add_executable(Animation)

file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

target_sources(Animation PRIVATE ${SRC_FILES} ${HED_FILES})

target_link_libraries(Animation PRIVATE Tipu)