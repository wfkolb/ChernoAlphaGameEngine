file(GLOB_RECURSE SCHEDULER_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(scheduler_tests ${SCHEDULER_TEST_SOURCES})
target_link_libraries(scheduler_tests PRIVATE
    engine::core
    GTest::gtest_main
)
target_include_directories(scheduler_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(scheduler_tests)

include(GoogleTest)
gtest_discover_tests(scheduler_tests PROPERTIES LABELS "unit")
