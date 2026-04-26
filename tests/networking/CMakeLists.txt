file(GLOB_RECURSE NETWORKING_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(networking_tests ${NETWORKING_TEST_SOURCES})
target_link_libraries(networking_tests PRIVATE
    engine::networking
    engine::core
    GTest::gtest_main
)
target_include_directories(networking_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/networking/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(networking_tests)

include(GoogleTest)
gtest_discover_tests(networking_tests PROPERTIES LABELS "integration")
