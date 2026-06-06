file(GLOB_RECURSE FPS_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(fps_tests ${FPS_TEST_SOURCES})
target_link_libraries(fps_tests PRIVATE
    engine::core
    GTest::gtest_main
)
target_include_directories(fps_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(fps_tests)

include(GoogleTest)
gtest_discover_tests(fps_tests PROPERTIES LABELS "unit")
