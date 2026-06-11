add_executable(app_tests
    ApplicationTests.cpp
    GameLoopTests.cpp
    DamageSystemTests.cpp
    CameraIntegrationTests.cpp
    TriggerCallbackTests.cpp
    SpawnExclusionTests.cpp
)

target_link_libraries(app_tests
    PRIVATE
        engine::app
        GTest::gtest_main
)

target_include_directories(app_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests"
    "${CMAKE_SOURCE_DIR}/src/app"        # for MeshRenderSystem.h (internal header)
)

engine_add_warnings(app_tests)

gtest_discover_tests(app_tests PROPERTIES LABELS "unit")
