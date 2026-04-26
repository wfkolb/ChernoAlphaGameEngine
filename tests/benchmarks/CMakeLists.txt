find_package(benchmark CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# core_bench
# ---------------------------------------------------------------------------
file(GLOB CORE_BENCH_SOURCES CONFIGURE_DEPENDS "core_bench.cpp")
add_executable(core_bench ${CORE_BENCH_SOURCES})
target_link_libraries(core_bench PRIVATE
    engine::core
    benchmark::benchmark_main
)
target_include_directories(core_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(core_bench)
add_test(NAME core_bench COMMAND core_bench)
set_tests_properties(core_bench PROPERTIES LABELS "benchmark")

# ---------------------------------------------------------------------------
# networking_bench
# ---------------------------------------------------------------------------
file(GLOB NET_BENCH_SOURCES CONFIGURE_DEPENDS "networking_bench.cpp")
add_executable(networking_bench ${NET_BENCH_SOURCES})
target_link_libraries(networking_bench PRIVATE
    engine::networking
    engine::core
    benchmark::benchmark_main
)
target_include_directories(networking_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src/networking/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(networking_bench)
add_test(NAME networking_bench COMMAND networking_bench)
set_tests_properties(networking_bench PROPERTIES LABELS "benchmark")

# ---------------------------------------------------------------------------
# tools_bench
# ---------------------------------------------------------------------------
file(GLOB TOOLS_BENCH_SOURCES CONFIGURE_DEPENDS "tools_bench.cpp")
add_executable(tools_bench ${TOOLS_BENCH_SOURCES})
target_link_libraries(tools_bench PRIVATE
    engine::tools
    engine::core
    benchmark::benchmark_main
)
target_include_directories(tools_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src/tools/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(tools_bench)
add_test(NAME tools_bench COMMAND tools_bench)
set_tests_properties(tools_bench PROPERTIES LABELS "benchmark")
