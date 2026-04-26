file(GLOB_RECURSE NETWORKING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER NETWORKING_SOURCES EXCLUDE REGEX ".*/public/.*")

add_library(engine_networking STATIC ${NETWORKING_SOURCES})
add_library(engine::networking ALIAS engine_networking)

target_include_directories(engine_networking
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal"
)

target_compile_features(engine_networking PUBLIC cxx_std_20)

engine_add_warnings(engine_networking)

target_link_libraries(engine_networking
    PUBLIC  engine::core
    PRIVATE ws2_32
)
