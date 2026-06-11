add_executable(input_tests
    InputSystemTests.cpp
    XInputTests.cpp
)

target_link_libraries(input_tests
    PRIVATE
        engine::core
        GTest::gtest_main
)

target_include_directories(input_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
)

engine_add_warnings(input_tests)

gtest_discover_tests(input_tests PROPERTIES LABELS "unit")
