cmake_minimum_required(VERSION 3.26)

set(DEMO_LINK_LIBS engine::rendering engine::tools engine::core)
set(DEMO_INTERNAL_INCLUDES "${CMAKE_SOURCE_DIR}/src/rendering/internal")

# ---- SpinDemo ----
add_executable(SpinDemo SpinDemo.cpp)
target_link_libraries(SpinDemo PRIVATE ${DEMO_LINK_LIBS})
target_include_directories(SpinDemo PRIVATE ${DEMO_INTERNAL_INCLUDES})
add_custom_command(TARGET SpinDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR="${CMAKE_BINARY_DIR}/shaders"
        -DDST_DIR="$<TARGET_FILE_DIR:SpinDemo>/shaders"
        -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
    COMMENT "Copying shaders to SpinDemo output (if present)")

# ---- ClearDemo ----
add_executable(ClearDemo ClearDemo.cpp)
target_link_libraries(ClearDemo PRIVATE ${DEMO_LINK_LIBS})

# ---- InputDemo ----
add_executable(InputDemo InputDemo.cpp)
target_link_libraries(InputDemo PRIVATE ${DEMO_LINK_LIBS} d3dcompiler.lib)
