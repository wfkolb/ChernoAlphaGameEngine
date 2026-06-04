file(GLOB_RECURSE SCENE_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(scene_tests ${SCENE_TEST_SOURCES})
target_link_libraries(scene_tests PRIVATE
    engine::core
    GTest::gtest_main
)
target_include_directories(scene_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(scene_tests)

include(GoogleTest)
gtest_discover_tests(scene_tests PROPERTIES LABELS "unit")
