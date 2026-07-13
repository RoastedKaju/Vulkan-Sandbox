add_executable(Offscreen)

file(GLOB_RECURSE SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE HED_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

target_sources(Offscreen PRIVATE ${SRC_FILES} ${HED_FILES})

target_link_libraries(Offscreen PRIVATE Tipu)