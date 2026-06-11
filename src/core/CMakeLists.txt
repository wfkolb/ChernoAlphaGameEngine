file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Exclude anything under public/ — those are headers, not translation units.
list(FILTER CORE_SOURCES EXCLUDE REGEX ".*/public/.*")

add_library(engine_core STATIC ${CORE_SOURCES})
add_library(engine::core ALIAS engine_core)

target_include_directories(engine_core
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal"
)

target_compile_features(engine_core PUBLIC cxx_std_20)

engine_add_warnings(engine_core)

# tomlplusplus is header-only; link it so vcpkg exposes the include path.
find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(engine_core PUBLIC tomlplusplus::tomlplusplus)

# XInput 1.4 — gamepad polling in Input.cpp.
target_link_libraries(engine_core PRIVATE Xinput.lib)
