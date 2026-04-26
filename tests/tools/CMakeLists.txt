file(GLOB_RECURSE TOOLS_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(tools_tests ${TOOLS_TEST_SOURCES})
target_link_libraries(tools_tests PRIVATE
    engine::tools
    engine::core
    GTest::gtest_main
)
target_include_directories(tools_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/tools/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(tools_tests)

include(GoogleTest)
gtest_discover_tests(tools_tests PROPERTIES LABELS "unit")
