# ── engine::app static library ───────────────────────────────────────────────
add_library(engine_app STATIC
    Engine.cpp
    Application.cpp
    SystemScheduler.cpp
    GameLoop.cpp
    DamageSystem.cpp
    MeshRenderSystem.cpp
)

target_compile_features(engine_app PUBLIC cxx_std_20)

engine_add_warnings(engine_app)

target_include_directories(engine_app
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../rendering/internal"
)

target_link_libraries(engine_app
    PUBLIC
        engine::core
        engine::rendering
        engine::networking
        engine::tools
        engine::physics
)

add_library(engine::app ALIAS engine_app)

# ── engine executable ─────────────────────────────────────────────────────────
add_executable(engine WIN32
    WinMain.cpp
)

engine_add_warnings(engine)

target_link_libraries(engine
    PRIVATE
        engine::app
)
