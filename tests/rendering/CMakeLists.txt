# ---------------------------------------------------------------------------
# rendering_tests — integration tests (label: "integration")
# ---------------------------------------------------------------------------
# Collects every .cpp except RenderingBench.cpp so the benchmark is kept in
# its own target with the "gpu" label.

file(GLOB_RECURSE RENDERING_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER RENDERING_TEST_SOURCES EXCLUDE REGEX ".*/RenderingBench\\.cpp$")

add_executable(rendering_tests ${RENDERING_TEST_SOURCES})
target_link_libraries(rendering_tests PRIVATE
    engine::rendering
    engine::core
    GTest::gtest_main
    d3d12 dxgi dxguid
)
target_include_directories(rendering_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rendering/public
    ${CMAKE_SOURCE_DIR}/src/core/public
    ${CMAKE_CURRENT_SOURCE_DIR}
)
engine_add_warnings(rendering_tests)

include(GoogleTest)
gtest_discover_tests(rendering_tests PROPERTIES LABELS "integration")

# ---------------------------------------------------------------------------
# rendering_bench — GPU benchmarks (label: "gpu")
#
# Run with:  ctest -L gpu
# Skipped by: ctest -L unit   (the default CI invocation)
# ---------------------------------------------------------------------------

add_executable(rendering_bench
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderingBench.cpp"
)
target_link_libraries(rendering_bench PRIVATE
    engine::rendering
    engine::core
    GTest::gtest_main
    d3d12 dxgi dxguid
)
target_include_directories(rendering_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rendering/public
    ${CMAKE_SOURCE_DIR}/src/core/public
    ${CMAKE_CURRENT_SOURCE_DIR}
)
engine_add_warnings(rendering_bench)

gtest_discover_tests(rendering_bench PROPERTIES LABELS "gpu")
