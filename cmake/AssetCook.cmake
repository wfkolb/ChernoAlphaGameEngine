# asset_cooker target is defined in src/tools/CMakeLists.txt once the cooker source exists.
# Until then this stub lets the rest of the build configure cleanly.

function(engine_cook_asset target source_asset output_easset)
    message(STATUS "[AssetCook] Stub: ${source_asset} -> ${output_easset}")
    # TODO: replace with the real add_custom_command once asset_cooker target exists.
    # add_custom_command(
    #     OUTPUT  ${output_easset}
    #     COMMAND $<TARGET_FILE:asset_cooker>
    #         --input  ${source_asset}
    #         --output ${output_easset}
    #     DEPENDS  ${source_asset} asset_cooker
    #     COMMENT  "Cooking asset ${source_asset}"
    #     VERBATIM
    # )
    # target_sources(${target} PRIVATE ${output_easset})
endfunction()
