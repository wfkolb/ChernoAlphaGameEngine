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

# engine_editor lib is always built — FPSGameEditor.exe links it without
# re-compiling editor sources. Only EngineEditor.exe (engine's own main) is
# gated on ENGINE_SKIP_EXTRAS, along with the standalone demo executables.
add_subdirectory(src/editor)

if(NOT ENGINE_SKIP_EXTRAS)
    add_subdirectory(src/demos)
endif()

if(NOT ENGINE_SKIP_TESTS)
    add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/samples")
    add_subdirectory(samples EXCLUDE_FROM_ALL)
endif()

# ── Code quality targets ────────────────────────────────────────────────────
find_program(CLANG_FORMAT_EXE NAMES clang-format)
find_program(CLANG_TIDY_EXE   NAMES clang-tidy)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE ALL_SOURCE_FILES
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/tests/*.cpp"
        "${CMAKE_SOURCE_DIR}/tests/*.h"
    )
    add_custom_target(clang-format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${ALL_SOURCE_FILES}
        COMMENT "Checking clang-format compliance"
        VERBATIM
    )
    add_custom_target(clang-format-fix
        COMMAND ${CLANG_FORMAT_EXE} -i ${ALL_SOURCE_FILES}
        COMMENT "Applying clang-format"
        VERBATIM
    )
endif()

if(CLANG_TIDY_EXE)
    add_custom_target(clang-tidy
        COMMAND ${CLANG_TIDY_EXE}
            -p "${CMAKE_BINARY_DIR}"
            ${ALL_SOURCE_FILES}
        COMMENT "Running clang-tidy"
        VERBATIM
    )
endif()
