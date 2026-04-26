// SpinDemo.cpp
// Loads a mesh (or uses unit-cube fallback), spins it around the Y-axis,
// and renders it with flat shading at 1280x720.
//
// Build dependencies: engine::rendering, engine::tools, engine::core
// Internal headers included below are permitted for an application binary.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

// ---- public engine headers ----
#include <rendering/Window.h>
#include <rendering/GpuDevice.h>
#include <rendering/FrameGraph.h>
#include <rendering/MeshManager.h>
#include <rendering/Camera.h>
#include <rendering/FlatShadePass.h>
#include <tools/AssetImporter.h>
#include <tools/EassetLoader.h>
#include <core/math/Quat.h>
#include <core/math/Mat.h>
#include <core/math/Transform.h>
#include <core/math/Vec.h>

// ---- internal rendering headers ----
// These expose FlatShadePipeline / createFlatShadePipeline / frameGraphSetDevice.
// SpinDemo is an application binary, not a library, so including internals is allowed.
// The demos CMakeLists adds src/rendering/internal to the include path.
#include <FlatShadePipeline.h>
#include <FrameGraphImpl.h>

// ---- d3d12 (for state enum values and device cast) ----
#include <d3d12.h>

// ---- standard library ----
#include <cstdlib>
#include <filesystem>
#include <string>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kWidth  = 1280;
constexpr uint32_t kHeight = 720;

// Clamp a float to [lo, hi].
constexpr float clamp(float v, float lo, float hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    // ------------------------------------------------------------------
    // 1. Import / load mesh
    // ------------------------------------------------------------------
    // If an argument is given, treat it as the glTF/glb source path.
    // On failure (or no argument), importGltf falls back to a unit cube.
    const std::filesystem::path sourcePath  = (argc > 1) ? argv[1] : "";
    const std::filesystem::path eassetPath  = std::filesystem::temp_directory_path()
                                              / "SpinDemo_mesh.easset";

    {
        engine::tools::ImportResult ir = engine::tools::importGltf(sourcePath, eassetPath);
        // importGltf always produces a valid .easset (unit-cube fallback on parse error).
        (void)ir;
    }

    auto cpuMeshOpt = engine::tools::loadEasset(eassetPath);
    if (!cpuMeshOpt.has_value()) {
        return EXIT_FAILURE;
    }
    const engine::tools::CpuMesh& cpuMesh = *cpuMeshOpt;

    // ------------------------------------------------------------------
    // 2. Create window
    // ------------------------------------------------------------------
    engine::rendering::Window window = engine::rendering::Window::create({
        .width  = kWidth,
        .height = kHeight,
        .title  = L"SpinDemo",
    });

    // ------------------------------------------------------------------
    // 3. Create GPU device
    // ------------------------------------------------------------------
    engine::rendering::GpuDevice device = engine::rendering::GpuDevice::create({
        .window = &window,
        .vsync  = true,
    });

    if (!device.isValid()) {
        return EXIT_FAILURE;
    }

    // ------------------------------------------------------------------
    // 4. Upload mesh to GPU
    // MeshManager captures the command list at construction time, so it
    // must be constructed while a frame is open (after beginFrame).
    // We run one dedicated upload frame before the render loop starts.
    // ------------------------------------------------------------------
    device.beginFrame();

    engine::rendering::MeshManager meshMgr(device);

    engine::rendering::MeshHandle meshHandle = meshMgr.uploadStatic(
        std::span<const engine::rendering::VertexStatic>(cpuMesh.vertices),
        std::span<const uint32_t>(cpuMesh.indices));

    device.endFrame();  // submit the copy commands
    device.flush();     // wait for GPU to finish the upload

    // ------------------------------------------------------------------
    // 5. Create flat-shade pipeline
    // ------------------------------------------------------------------
    auto* d3dDevice = static_cast<ID3D12Device*>(device.nativeDevice());

    // 87 = DXGI_FORMAT_R8G8B8A8_UNORM, 20 = DXGI_FORMAT_D32_FLOAT
    engine::rendering::FlatShadePipeline flatPipeline =
        engine::rendering::createFlatShadePipeline(d3dDevice, 87u, 20u);

    // ------------------------------------------------------------------
    // 6. Wire FrameGraph to device
    // ------------------------------------------------------------------
    engine::rendering::FrameGraph fg;
    engine::rendering::frameGraphSetDevice(fg, device.nativeDevice());

    // ------------------------------------------------------------------
    // 7. Camera setup
    //    Position: (0, 1.5, -4), identity rotation, fovY = π/3
    // ------------------------------------------------------------------
    engine::core::math::Transform camTransform{};
    camTransform.position = { 0.0f, 1.5f, 4.0f };
    camTransform.rotation = engine::core::math::Quat::identity();
    camTransform.scale    = { 1.0f, 1.0f, 1.0f };

    engine::rendering::Camera cam{};
    cam.fovYDegrees = 60.0f;   // π/3 radians ≈ 60°
    cam.nearZ       = 0.1f;
    cam.farZ        = 100.0f;
    cam.isMain      = true;

    // ------------------------------------------------------------------
    // 8. QPC timer
    // ------------------------------------------------------------------
    LARGE_INTEGER qpcFreq, qpcLast;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcLast);

    float angleRad = 0.0f;

    // ------------------------------------------------------------------
    // 9. Main loop
    // ------------------------------------------------------------------
    while (!window.wantsClose()) {
        // -- Compute dt --
        LARGE_INTEGER qpcNow;
        QueryPerformanceCounter(&qpcNow);
        const float dt = clamp(
            static_cast<float>(qpcNow.QuadPart - qpcLast.QuadPart) /
            static_cast<float>(qpcFreq.QuadPart),
            0.0f, 0.1f);
        qpcLast = qpcNow;

        // -- Update rotation --
        angleRad += dt * 1.0f; // 1 radian/second around Y

        // -- Build MVP (row-major, world * view * proj) --
        // Row-vector convention: v' = v * MVP,  HLSL: mul(v, MVP)
        const engine::core::math::Mat4 world =
            engine::core::math::toMat4(
                engine::core::math::fromAxisAngle(
                    engine::core::math::Vec3{0.0f, 1.0f, 0.0f},
                    angleRad));

        const engine::core::math::Mat4 view =
            engine::rendering::cameraViewMatrix(camTransform);

        const engine::core::math::Mat4 proj =
            engine::rendering::cameraProjMatrix(cam,
                static_cast<float>(kWidth) / static_cast<float>(kHeight));

        // MVP: world * view * proj
        const engine::core::math::Mat4 mvp = world * view * proj;

        // -- Render --
        device.beginFrame();

        fg.reset();

        // Import back buffer (currently in D3D12_RESOURCE_STATE_RENDER_TARGET = 0x4,
        // because beginFrame() already transitioned it).
        const engine::rendering::ResourceHandle bb =
            fg.importBackBuffer(
                device.nativeBackBuffer(),
                device.currentBackBufferRtvHandle(),
                static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));

        // Import depth buffer (importDepthBuffer always marks it DEPTH_WRITE internally).
        const engine::rendering::ResourceHandle db =
            fg.importDepthBuffer(
                device.nativeDepthBuffer(),
                device.depthBufferDsvHandle());

        // Add the flat-shade pass — consumes &mvp.m[0][0] as 16 floats
        engine::rendering::addFlatShadePass(
            fg,
            meshMgr,
            meshHandle,
            flatPipeline,
            &mvp.m[0][0],
            bb,
            db,
            kWidth,
            kHeight);

        fg.compile();
        fg.execute(device.nativeCommandList());

        device.endFrame();
    }

    // ------------------------------------------------------------------
    // 10. Shutdown
    // ------------------------------------------------------------------
    device.flush();

    // Clean up temp easset file (best-effort; ignore errors)
    std::filesystem::remove(eassetPath);

    return EXIT_SUCCESS;
}
