set(ENGINE_COMPILE_FLAGS
    /W4              # Warning level 4: comprehensive diagnostics
    /WX              # Treat warnings as errors: enforces zero-warning policy
    /permissive-     # Strict conformance mode: rejects non-standard extensions
    /Zc:__cplusplus  # Report the correct value of __cplusplus (MSVC lies by default)
    /Zc:preprocessor # Use the new conforming preprocessor required for __VA_OPT__
    /wd4201          # Nameless struct/union: DX12 headers use this extension pervasively
    /wd4324          # Structure padded due to alignment: expected when Vec3/Quat (alignas(16)) mix with scalars
    /wd5105          # Windows SDK 10.0.19041.0 winbase.h macro produces 'defined' (SDK bug)
)

function(engine_add_warnings target)
    target_compile_options(${target} PRIVATE ${ENGINE_COMPILE_FLAGS})
endfunction()

# ASAN for Debug builds — catches memory errors in unit test runs.
# Call engine_add_asan(target) after engine_add_warnings(target) for any
# target that runs under ctest. MSVC ASAN is supported on windows-2022
# runners; no extra runtime linkage is needed when using /fsanitize=address
# with the MSVC toolset bundled in VS 2022 17.x.
function(engine_add_asan target)
    if(MSVC AND (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "DEBUG"))
        target_compile_options(${target} PRIVATE /fsanitize=address)
    endif()
endfunction()
