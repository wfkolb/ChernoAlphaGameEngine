add_executable(log_tests
    LogTraceElisionTest.cpp
)

target_link_libraries(log_tests
    PRIVATE
        engine::core
        GTest::gtest_main
)

target_include_directories(log_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
)

engine_add_warnings(log_tests)

gtest_discover_tests(log_tests PROPERTIES LABELS "unit")
