cmake_minimum_required(VERSION 3.26)

add_executable(spin_demo_tests SpinDemoTest.cpp)

target_link_libraries(spin_demo_tests PRIVATE
    engine::rendering
    engine::tools
    engine::core
    GTest::gtest_main)

target_include_directories(spin_demo_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rendering/public
    ${CMAKE_SOURCE_DIR}/src/tools/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)

engine_add_warnings(spin_demo_tests)

include(GoogleTest)
gtest_discover_tests(spin_demo_tests PROPERTIES LABELS "integration")
