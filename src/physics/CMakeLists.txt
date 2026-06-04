file(GLOB_RECURSE PHYSICS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER PHYSICS_SOURCES EXCLUDE REGEX ".*/public/.*")

add_library(engine_physics STATIC ${PHYSICS_SOURCES})
add_library(engine::physics ALIAS engine_physics)

target_include_directories(engine_physics
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal"
)

target_compile_features(engine_physics PUBLIC cxx_std_20)

engine_add_warnings(engine_physics)

find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(engine_physics
    PUBLIC  engine::core
    PRIVATE tomlplusplus::tomlplusplus
)
