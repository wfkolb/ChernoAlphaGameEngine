# ---------------------------------------------------------------------------
# rendering_unit_tests — pure-CPU unit tests (label: "unit")
# ---------------------------------------------------------------------------
# Tests that exercise rendering systems without a GPU / D3D12 device.

add_executable(rendering_unit_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/LightBufferTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/IblTests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ShadowSplitTests.cpp"
)
target_link_libraries(rendering_unit_tests PRIVATE
    engine::rendering
    engine::tools        # IblCooker, EassetLoader (IblData, loadIblEasset)
    engine::core
    GTest::gtest_main
)
target_include_directories(rendering_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rendering/public
    ${CMAKE_SOURCE_DIR}/src/rendering          # allows "internal/LightCullSystem.h"
    ${CMAKE_SOURCE_DIR}/src/tools/public
    ${CMAKE_SOURCE_DIR}/src/core/public
    ${CMAKE_CURRENT_SOURCE_DIR}
)
engine_add_warnings(rendering_unit_tests)
gtest_discover_tests(rendering_unit_tests PROPERTIES LABELS "unit")

# ---------------------------------------------------------------------------
# rendering_tests — integration tests (label: "integration")
# ---------------------------------------------------------------------------
# Collects every .cpp except RenderingBench.cpp and LightBufferTests.cpp so
# the benchmark is kept in its own target with the "gpu" label and the unit
# tests run in their dedicated target above.

file(GLOB_RECURSE RENDERING_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER RENDERING_TEST_SOURCES EXCLUDE REGEX ".*/RenderingBench\\.cpp$")
list(FILTER RENDERING_TEST_SOURCES EXCLUDE REGEX ".*/LightBufferTests\\.cpp$")
list(FILTER RENDERING_TEST_SOURCES EXCLUDE REGEX ".*/IblTests\\.cpp$")
list(FILTER RENDERING_TEST_SOURCES EXCLUDE REGEX ".*/ShadowSplitTests\\.cpp$")

add_executable(rendering_tests ${RENDERING_TEST_SOURCES})
target_link_libraries(rendering_tests PRIVATE
    engine::rendering
    engine::core
    GTest::gtest_main
    d3d12 dxgi dxguid
)

# stb headers (stb_image.h, stb_image_write.h) — used by GoldenCompare and StbImpl.
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)

target_include_directories(rendering_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rendering/public
    ${CMAKE_SOURCE_DIR}/src/rendering/internal
    ${CMAKE_SOURCE_DIR}/src/core/public
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STB_INCLUDE_DIRS}
)
engine_add_warnings(rendering_tests)

# Copy compiled shaders next to the test binary so OpaqueVS.cso etc. are loadable.
add_custom_command(TARGET rendering_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR="${CMAKE_BINARY_DIR}/shaders"
        -DDST_DIR="$<TARGET_FILE_DIR:rendering_tests>/shaders"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/copy_if_exists.cmake"
    COMMENT "Copying shaders to rendering_tests output dir"
)

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
