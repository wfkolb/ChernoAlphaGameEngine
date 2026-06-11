cmake_minimum_required(VERSION 3.26)

# Editor split: engine_editor static library (all sources except main.cpp)
# + EngineEditor.exe (thin entry, engine's own main.cpp).
#
# FPSGameEditor.exe in the FPSGame project links engine::editor and provides
# its own main (FPSGameEditorMain.cpp) that registers game-specific archetypes
# before the first frame — without the engine having compile-time knowledge of
# any game library.

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
# Exclude the engine's own main.cpp — every editor executable provides its own.
list(FILTER EDITOR_SOURCES EXCLUDE REGEX ".*/editor/main\\.cpp$")

# ── engine_editor — linkable editor library ───────────────────────────────────
add_library(engine_editor STATIC ${EDITOR_SOURCES})
add_library(engine::editor ALIAS engine_editor)

# "editor/..." includes resolve from engine/src/ root.
target_include_directories(engine_editor
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../rendering/internal"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app"
)

target_compile_features(engine_editor PUBLIC cxx_std_20)

if(ENGINE_DEVREL)
    target_compile_definitions(engine_editor PUBLIC ENGINE_DEVREL)
endif()

engine_add_warnings(engine_editor)

target_link_libraries(engine_editor
    PUBLIC engine::core
           engine::rendering
           engine::tools
           engine::physics
           engine::app
)

find_package(imgui CONFIG REQUIRED)
target_link_libraries(engine_editor PUBLIC imgui::imgui)
find_package(imguizmo CONFIG REQUIRED)
target_link_libraries(engine_editor PUBLIC imguizmo::imguizmo)
target_link_libraries(engine_editor PUBLIC d3d12.lib dxgi.lib)

# ── EngineEditor.exe — engine's own (generic) editor entry ────────────────────
# Skipped when building as a subdirectory inside a game project (ENGINE_SKIP_EXTRAS).
if(NOT ENGINE_SKIP_EXTRAS)
    add_executable(EngineEditor WIN32 main.cpp)
    engine_add_warnings(EngineEditor)
    target_link_libraries(EngineEditor PRIVATE engine::editor)
    add_custom_command(TARGET EngineEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR="${CMAKE_BINARY_DIR}/shaders"
            -DDST_DIR="$<TARGET_FILE_DIR:EngineEditor>/shaders"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
        COMMENT "Copying shaders to EngineEditor output dir"
    )
endif()
