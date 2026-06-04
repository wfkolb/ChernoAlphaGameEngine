cmake_minimum_required(VERSION 3.26)

# EngineEditor is a DevRel-only tool. When ENGINE_DEVREL is not enabled the
# target still builds (main.cpp compiles a trivial stub) so the build graph
# stays consistent across configs.

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(EngineEditor WIN32 ${EDITOR_SOURCES})

# Headers are included as "editor/..." — expose the src/ root so they resolve.
target_include_directories(EngineEditor
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_features(EngineEditor PRIVATE cxx_std_20)

# The editor's entire implementation lives behind ENGINE_DEVREL.
if(ENGINE_DEVREL)
    target_compile_definitions(EngineEditor PRIVATE ENGINE_DEVREL)
endif()

engine_add_warnings(EngineEditor)

target_link_libraries(EngineEditor
    PRIVATE engine::core
    PRIVATE engine::rendering
    PRIVATE engine::tools
    PRIVATE engine::physics
)

find_package(imgui CONFIG REQUIRED)
target_link_libraries(EngineEditor PRIVATE imgui::imgui)

target_link_libraries(EngineEditor PRIVATE d3d12.lib dxgi.lib)
