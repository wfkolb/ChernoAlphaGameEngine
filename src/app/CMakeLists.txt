add_executable(engine WIN32
    WinMain.cpp
    Engine.cpp
)

target_compile_features(engine PRIVATE cxx_std_20)

engine_add_warnings(engine)

target_include_directories(engine
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/public"
)

target_link_libraries(engine
    PRIVATE
        engine::core
        engine::rendering
        engine::networking
        engine::tools
)
