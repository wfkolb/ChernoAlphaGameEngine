# ── Integration tests (require real network / socket) ────────────────────────
add_executable(networking_tests
    LoopbackTest.cpp
    StressTest.cpp
    PredictionIntegrationTests.cpp
)
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

# ── Unit tests ────────────────────────────────────────────────────────────────
add_executable(networking_unit_tests
    ReplicationTests.cpp
    PredictionTests.cpp
    RpcValidationTests.cpp
    ObfuscationTests.cpp
    SerializerFuzzTests.cpp
    InputMessageTests.cpp
)
target_link_libraries(networking_unit_tests PRIVATE
    engine::networking
    engine::app
    engine::core
    GTest::gtest_main
)
target_include_directories(networking_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/networking/public
    ${CMAKE_SOURCE_DIR}/src/app/public
    ${CMAKE_SOURCE_DIR}/src/core/public
)
engine_add_warnings(networking_unit_tests)

gtest_discover_tests(networking_unit_tests PROPERTIES LABELS "unit")
