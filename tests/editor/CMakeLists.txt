file(GLOB_RECURSE EDITOR_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

# Pull in the editor source files that contain pure-data logic (no DX12/ImGui).
set(EDITOR_IMPL_SOURCES
    "${CMAKE_SOURCE_DIR}/src/editor/EditorPrefs.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/MetaFileWriter.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/SelectionSystem.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/commands/EntityCommand.cpp"
    "${CMAKE_SOURCE_DIR}/src/editor/UndoStack.cpp"
)

add_executable(editor_tests
    ${EDITOR_TEST_SOURCES}
    ${EDITOR_IMPL_SOURCES}
)

# All editor code is behind ENGINE_DEVREL; define it for the test target so the
# headers and .cpp files compile. No DX12 / ImGui is used in these tests.
target_compile_definitions(editor_tests PRIVATE ENGINE_DEVREL)

target_link_libraries(editor_tests PRIVATE
    engine::tools
    engine::core
    GTest::gtest_main
)

# tomlplusplus is PRIVATE in engine::tools so we must link it explicitly here
# because EditorPrefs.cpp and MetaFileWriter.cpp include <toml++/toml.hpp>.
find_package(tomlplusplus CONFIG REQUIRED)
target_link_libraries(editor_tests PRIVATE tomlplusplus::tomlplusplus)

target_include_directories(editor_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/tools/public
    ${CMAKE_SOURCE_DIR}/src/core/public
    ${CMAKE_SOURCE_DIR}/src
)

engine_add_warnings(editor_tests)

include(GoogleTest)
gtest_discover_tests(editor_tests PROPERTIES LABELS "unit")
