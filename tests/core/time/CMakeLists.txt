file(GLOB_RECURSE TIME_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(core_time_tests ${TIME_TEST_SOURCES})
target_link_libraries(core_time_tests PRIVATE
    engine::core
    GTest::gtest_main
)
engine_add_warnings(core_time_tests)

include(GoogleTest)
gtest_discover_tests(core_time_tests PROPERTIES LABELS "unit")
