set(ENGINE_COMPILE_FLAGS
    /W4              # Warning level 4: comprehensive diagnostics
    /WX              # Treat warnings as errors: enforces zero-warning policy
    /permissive-     # Strict conformance mode: rejects non-standard extensions
    /Zc:__cplusplus  # Report the correct value of __cplusplus (MSVC lies by default)
    /Zc:preprocessor # Use the new conforming preprocessor required for __VA_OPT__
    /wd4201          # Nameless struct/union: DX12 headers use this extension pervasively
    /wd5105          # Windows SDK 10.0.19041.0 winbase.h macro produces 'defined' (SDK bug)
)

function(engine_add_warnings target)
    target_compile_options(${target} PRIVATE ${ENGINE_COMPILE_FLAGS})
endfunction()
