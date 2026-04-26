# DXC_EXECUTABLE is resolved from third_party/dxc/ once the vendored DXC is present.
# Until then this stub lets the rest of the build configure cleanly.

function(engine_compile_shader target shader_file entry profile output)
    message(STATUS "[ShaderCompile] Stub: ${shader_file} -> ${output} (DXC not yet wired)")
    # TODO: replace with the real add_custom_command once DXC_EXECUTABLE is set.
    # set(output_path "${CMAKE_CURRENT_BINARY_DIR}/shaders/${output}")
    # add_custom_command(
    #     OUTPUT  ${output_path}
    #     COMMAND ${DXC_EXECUTABLE}
    #         -T ${profile}
    #         -E ${entry}
    #         -Fo ${output_path}
    #         -I  ${ENGINE_SHADER_DIR}
    #         -WX -Ges -Qstrip_reflect -O3
    #         -MD -MF "${output_path}.d"
    #         $<$<CONFIG:Debug>:-Od -Zi>
    #         $<$<CONFIG:RelWithDebInfo>:-Od -Zi>
    #         ${shader_file}
    #     DEPENDS  ${shader_file}
    #     DEPFILE  "${output_path}.d"
    #     COMMENT  "Compiling shader ${shader_file}"
    #     VERBATIM
    # )
    # target_sources(${target} PRIVATE ${output_path})
endfunction()
