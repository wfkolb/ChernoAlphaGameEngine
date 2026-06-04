file(GLOB_RECURSE TOOLS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER TOOLS_SOURCES EXCLUDE REGEX ".*/public/.*")
list(FILTER TOOLS_SOURCES EXCLUDE REGEX ".*/asset_cooker/.*")

add_library(engine_tools STATIC ${TOOLS_SOURCES})
add_library(engine::tools ALIAS engine_tools)

target_include_directories(engine_tools
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal"
)

target_compile_features(engine_tools PUBLIC cxx_std_20)

engine_add_warnings(engine_tools)

target_link_libraries(engine_tools
    PUBLIC engine::core
    PUBLIC engine::rendering
)

find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(engine_tools PRIVATE tomlplusplus::tomlplusplus)

find_package(lz4 CONFIG REQUIRED)
target_link_libraries(engine_tools PRIVATE lz4::lz4)

find_package(imgui CONFIG REQUIRED)
target_link_libraries(engine_tools PRIVATE imgui::imgui)

find_package(meshoptimizer CONFIG REQUIRED)
target_link_libraries(engine_tools PRIVATE meshoptimizer::meshoptimizer)

find_path(CGLTF_INCLUDE_DIRS "cgltf.h" REQUIRED)
target_include_directories(engine_tools PRIVATE ${CGLTF_INCLUDE_DIRS})

# ---------------------------------------------------------------------------
# Offline asset cooker executable (Task #33)
# ---------------------------------------------------------------------------
add_executable(asset_cooker
    "${CMAKE_CURRENT_SOURCE_DIR}/asset_cooker/main.cpp"
)
target_link_libraries(asset_cooker PRIVATE engine::tools engine::core)
target_compile_features(asset_cooker PRIVATE cxx_std_20)
engine_add_warnings(asset_cooker)
