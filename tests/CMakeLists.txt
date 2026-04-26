find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_subdirectory(core)
add_subdirectory(networking)
add_subdirectory(rendering)
add_subdirectory(tools)
add_subdirectory(benchmarks)
add_subdirectory(demos)
