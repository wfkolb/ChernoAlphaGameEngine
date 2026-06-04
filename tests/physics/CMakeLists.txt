file(GLOB_RECURSE PHYSICS_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(physics_tests ${PHYSICS_TEST_SOURCES})
target_link_libraries(physics_tests PRIVATE
    engine::physics
    GTest::gtest_main
)
target_include_directories(physics_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/physics/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(physics_tests)

include(GoogleTest)
gtest_discover_tests(physics_tests PROPERTIES LABELS "unit")
