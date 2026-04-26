file(GLOB_RECURSE RENDERING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
list(FILTER RENDERING_SOURCES EXCLUDE REGEX ".*/public/.*")

add_library(engine_rendering STATIC ${RENDERING_SOURCES})
add_library(engine::rendering ALIAS engine_rendering)

target_include_directories(engine_rendering
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/public"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/internal"
)

target_compile_features(engine_rendering PUBLIC cxx_std_20)

engine_add_warnings(engine_rendering)

target_link_libraries(engine_rendering
    PUBLIC  engine::core
    PRIVATE d3d12 dxgi dxguid
)

find_package(directx-headers CONFIG REQUIRED)
target_link_libraries(engine_rendering PRIVATE Microsoft::DirectX-Headers)

# ---------------------------------------------------------------------------
# Shader compilation via DXC
# Output .cso files go to ${CMAKE_BINARY_DIR}/shaders/ so the executable can
# find them at runtime as shaders/FlatShadeVS.cso etc.
# ---------------------------------------------------------------------------
find_program(DXC_EXECUTABLE dxc
    HINTS
        "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc"
        "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows-static/tools/directx-dxc"
)

if(DXC_EXECUTABLE)
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

    # FlatShadeVS — vs_6_0
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/FlatShadeVS.cso"
        COMMAND "${DXC_EXECUTABLE}"
                -T vs_6_0
                -E main
                -Fo "${SHADER_OUTPUT_DIR}/FlatShadeVS.cso"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/FlatShadeVS.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/FlatShadeVS.hlsl"
        COMMENT "Compiling FlatShadeVS.hlsl -> FlatShadeVS.cso"
        VERBATIM
    )

    # FlatShadePS — ps_6_0
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/FlatShadePS.cso"
        COMMAND "${DXC_EXECUTABLE}"
                -T ps_6_0
                -E main
                -Fo "${SHADER_OUTPUT_DIR}/FlatShadePS.cso"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/FlatShadePS.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/FlatShadePS.hlsl"
        COMMENT "Compiling FlatShadePS.hlsl -> FlatShadePS.cso"
        VERBATIM
    )

    add_custom_target(engine_rendering_shaders ALL
        DEPENDS
            "${SHADER_OUTPUT_DIR}/FlatShadeVS.cso"
            "${SHADER_OUTPUT_DIR}/FlatShadePS.cso"
    )
    add_dependencies(engine_rendering engine_rendering_shaders)
else()
    message(WARNING "DXC not found — shaders will not be compiled. SpinDemo requires DXC at build time.")
endif()
