file(GLOB_RECURSE ECS_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(ecs_tests ${ECS_TEST_SOURCES})
target_link_libraries(ecs_tests PRIVATE
    engine::core
    GTest::gtest_main
)
target_include_directories(ecs_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(ecs_tests)

include(GoogleTest)
gtest_discover_tests(ecs_tests PROPERTIES LABELS "unit")
