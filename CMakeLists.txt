cmake_minimum_required(VERSION 3.26)
project(Engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# vcpkg toolchain is set externally via --preset / CMAKE_TOOLCHAIN_FILE.
# We do not hardcode its path here.

include(cmake/Warnings.cmake)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/rendering)
add_subdirectory(src/networking)
add_subdirectory(src/tools)
add_subdirectory(src/physics)
add_subdirectory(src/app)
add_subdirectory(src/demos)
add_subdirectory(src/editor)
add_subdirectory(tests)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/samples")
    add_subdirectory(samples EXCLUDE_FROM_ALL)
endif()
