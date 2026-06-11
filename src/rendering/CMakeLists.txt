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
# Source HLSL lives in ${CMAKE_SOURCE_DIR}/shaders/<pass>/.
# Output .cso files go to ${CMAKE_BINARY_DIR}/shaders/ so executables can
# find them at runtime as shaders/OpaqueVS.cso etc.
# ---------------------------------------------------------------------------
find_program(DXC_EXECUTABLE dxc
    HINTS
        "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/tools/directx-dxc"
        "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows-static/tools/directx-dxc"
)

if(DXC_EXECUTABLE)
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    # Use the engine's own shaders directory regardless of which project is top-level.
    # CMAKE_CURRENT_SOURCE_DIR is engine/src/rendering; ../../shaders reaches engine/shaders.
    get_filename_component(SHADER_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/../../shaders" ABSOLUTE)
    file(MAKE_DIRECTORY "${SHADER_OUTPUT_DIR}")

    # OpaqueVS — vs_6_6
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/OpaqueVS.cso"
        COMMAND "${DXC_EXECUTABLE}" -T vs_6_6 -E main
                -Fo "${SHADER_OUTPUT_DIR}/OpaqueVS.cso"
                "${SHADER_SOURCE_DIR}/opaque/OpaqueVS.hlsl"
        DEPENDS "${SHADER_SOURCE_DIR}/opaque/OpaqueVS.hlsl"
                "${SHADER_SOURCE_DIR}/common/CommonTypes.hlsli"
        COMMENT "Compiling OpaqueVS.hlsl -> OpaqueVS.cso"
        VERBATIM
    )

    # OpaquePS — ps_6_6
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/OpaquePS.cso"
        COMMAND "${DXC_EXECUTABLE}" -T ps_6_6 -E main
                -Fo "${SHADER_OUTPUT_DIR}/OpaquePS.cso"
                "${SHADER_SOURCE_DIR}/opaque/OpaquePS.hlsl"
        DEPENDS "${SHADER_SOURCE_DIR}/opaque/OpaquePS.hlsl"
                "${SHADER_SOURCE_DIR}/common/CommonTypes.hlsli"
        COMMENT "Compiling OpaquePS.hlsl -> OpaquePS.cso"
        VERBATIM
    )

    # ShadowVS — vs_6_6
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/ShadowVS.cso"
        COMMAND "${DXC_EXECUTABLE}" -T vs_6_6 -E main
                -Fo "${SHADER_OUTPUT_DIR}/ShadowVS.cso"
                "${SHADER_SOURCE_DIR}/shadow/ShadowVS.hlsl"
        DEPENDS "${SHADER_SOURCE_DIR}/shadow/ShadowVS.hlsl"
                "${SHADER_SOURCE_DIR}/common/CommonTypes.hlsli"
        COMMENT "Compiling ShadowVS.hlsl -> ShadowVS.cso"
        VERBATIM
    )

    # ShadowPS — ps_6_6
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/ShadowPS.cso"
        COMMAND "${DXC_EXECUTABLE}" -T ps_6_6 -E main
                -Fo "${SHADER_OUTPUT_DIR}/ShadowPS.cso"
                "${SHADER_SOURCE_DIR}/shadow/ShadowPS.hlsl"
        DEPENDS "${SHADER_SOURCE_DIR}/shadow/ShadowPS.hlsl"
        COMMENT "Compiling ShadowPS.hlsl -> ShadowPS.cso"
        VERBATIM
    )

    # DebugLineVS — vs_6_6
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/DebugLineVS.cso"
        COMMAND "${DXC_EXECUTABLE}" -T vs_6_6 -E main
                -Fo "${SHADER_OUTPUT_DIR}/DebugLineVS.cso"
                "${SHADER_SOURCE_DIR}/debug/DebugLineVS.hlsl"
        DEPENDS "${SHADER_SOURCE_DIR}/debug/DebugLineVS.hlsl"
        COMMENT "Compiling DebugLineVS.hlsl -> DebugLineVS.cso"
        VERBATIM
    )

    # DebugLinePS — ps_6_6
    add_custom_command(
        OUTPUT  "${SHADER_OUTPUT_DIR}/DebugLinePS.cso"
        COMMAND "${DXC_EXECUTABLE}" -T ps_6_6 -E main
                -Fo "${SHADER_OUTPUT_DIR}/DebugLinePS.cso"
                "${SHADER_SOURCE_DIR}/debug/DebugLinePS.hlsl"
        DEPENDS "${SHADER_SOURCE_DIR}/debug/DebugLinePS.hlsl"
        COMMENT "Compiling DebugLinePS.hlsl -> DebugLinePS.cso"
        VERBATIM
    )

    add_custom_target(engine_rendering_shaders ALL
        DEPENDS
            "${SHADER_OUTPUT_DIR}/OpaqueVS.cso"
            "${SHADER_OUTPUT_DIR}/OpaquePS.cso"
            "${SHADER_OUTPUT_DIR}/ShadowVS.cso"
            "${SHADER_OUTPUT_DIR}/ShadowPS.cso"
            "${SHADER_OUTPUT_DIR}/DebugLineVS.cso"
            "${SHADER_OUTPUT_DIR}/DebugLinePS.cso"
    )
    add_dependencies(engine_rendering engine_rendering_shaders)
else()
    message(WARNING "DXC not found — shaders will not be compiled. SpinDemo requires DXC at build time.")
endif()
