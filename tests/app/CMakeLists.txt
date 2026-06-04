add_executable(app_tests
    ApplicationTests.cpp
    GameLoopTests.cpp
    DamageSystemTests.cpp
)

target_link_libraries(app_tests
    PRIVATE
        engine::app
        GTest::gtest_main
)

target_include_directories(app_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
)

engine_add_warnings(app_tests)

gtest_discover_tests(app_tests PROPERTIES LABELS "unit")
