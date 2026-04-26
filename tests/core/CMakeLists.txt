file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/math/*.cpp"
)

add_executable(core_tests ${CORE_TEST_SOURCES})

target_link_libraries(core_tests
    PRIVATE
        engine::core
        GTest::gtest_main
)

# Allows test support headers to be found as <support/Foo.h>
target_include_directories(core_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
)

engine_add_warnings(core_tests)

gtest_discover_tests(core_tests PROPERTIES LABELS "unit")

add_subdirectory(ecs)
