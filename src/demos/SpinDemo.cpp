// SpinDemo.cpp
// Loads a mesh (or uses unit-cube fallback), spins it around the Y-axis,
// and renders it with PBR lighting at 1280x720.
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
#include <rendering/Material.h>
#include <tools/AssetImporter.h>
#include <tools/EassetLoader.h>
#include <core/math/Quat.h>
#include <core/math/Mat.h>
#include <core/math/Transform.h>
#include <core/math/Vec.h>

// ---- internal rendering headers ----
// PipelineState.h (createOpaquePassPipeline) and frameGraphSetDevice live
// in internal/. SpinDemo is an exe (not a library), so including internals is allowed.
// The demos CMakeLists adds src/rendering/internal to the include path.
#include <PipelineState.h>
#include <FrameGraphImpl.h>

// ---- d3d12 ----
#include <d3d12.h>
#include <wrl/client.h>

// ---- standard library ----
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kWidth  = 1280;
constexpr uint32_t kHeight = 720;

// Scroll-wheel zoom — accumulated by ZoomWndProc, consumed in the main loop.
static WNDPROC g_origWndProc = nullptr;
static int     g_wheelAccum  = 0;

static LRESULT CALLBACK ZoomWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEWHEEL)
        g_wheelAccum += GET_WHEEL_DELTA_WPARAM(wp);
    return CallWindowProcW(g_origWndProc, h, msg, wp, lp);
}

constexpr float clampf(float v, float lo, float hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Load a compiled shader object (.cso) from a path relative to the executable.
// Returns an empty vector on failure.
std::vector<uint8_t> loadCso(const wchar_t* relPath) {
    wchar_t exePath[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};

    wchar_t* lastSep = nullptr;
    for (wchar_t* p = exePath; *p; ++p)
        if (*p == L'\\' || *p == L'/') lastSep = p;
    if (!lastSep) return {};
    *(lastSep + 1) = L'\0';

    wchar_t full[MAX_PATH] = {};
    if (wcscat_s(full, exePath) != 0) return {};
    if (wcscat_s(full, relPath) != 0) return {};

    HANDLE h = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER sz = {};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) { CloseHandle(h); return {}; }

    std::vector<uint8_t> data(static_cast<size_t>(sz.QuadPart));
    DWORD bytesRead = 0;
    bool ok = ReadFile(h, data.data(), static_cast<DWORD>(data.size()),
                       &bytesRead, nullptr) != FALSE
           && bytesRead == static_cast<DWORD>(data.size());
    CloseHandle(h);
    return ok ? data : std::vector<uint8_t>{};
}

// Create an upload-heap committed buffer.
bool createUploadBuffer(ID3D12Device* dev, UINT64 bytes,
                        Microsoft::WRL::ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type                  = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd   = {};
    rd.Dimension             = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width                 = bytes;
    rd.Height                = 1;
    rd.DepthOrArraySize      = 1;
    rd.MipLevels             = 1;
    rd.Format                = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc            = { 1, 0 };
    rd.Layout                = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return SUCCEEDED(dev->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out)));
}

// CPU mirror of HLSL PerFrameConstants (512 bytes — see shaders/common/CommonTypes.hlsli).
struct PerFrameData {
    float    viewMatrix[16];
    float    projMatrix[16];
    float    viewProjMatrix[16];
    float    cameraWorldPos[3];
    float    pad0;
    float    time;
    uint32_t lightCount;
    uint32_t hasIbl;
    float    pad1;
    float    shadowCascadeMat[4][16];
    float    cascadeSplits[4];
    uint32_t hasShadows;
    float    pad2[3];
};
static_assert(sizeof(PerFrameData) == 512,
    "PerFrameData must match HLSL PerFrameConstants (512 bytes)");

// CPU mirror of HLSL PerObjectConstants (80 bytes).
struct PerObjectData {
    float    worldMatrix[16];
    uint32_t materialIndex;
    float    pad[3];
};
static_assert(sizeof(PerObjectData) == 80,
    "PerObjectData must match HLSL PerObjectConstants (80 bytes)");

} // anonymous namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    // ------------------------------------------------------------------
    // 1. Import / load mesh
    // ------------------------------------------------------------------
    const std::filesystem::path sourcePath  = (argc > 1) ? argv[1] : "";
    const std::filesystem::path eassetPath  =
        std::filesystem::temp_directory_path() / "SpinDemo_mesh.easset";

    {
        engine::tools::ImportResult ir = engine::tools::importGltf(sourcePath, eassetPath);
        (void)ir;
    }

    auto cpuMeshOpt = engine::tools::loadEasset(eassetPath);
    if (!cpuMeshOpt.has_value()) return EXIT_FAILURE;
    const engine::tools::CpuMesh& cpuMesh = *cpuMeshOpt;

    // ------------------------------------------------------------------
    // 2. Create window
    // ------------------------------------------------------------------
    engine::rendering::Window window = engine::rendering::Window::create({
        .width  = kWidth,
        .height = kHeight,
        .title  = L"SpinDemo",
    });

    // Subclass the WndProc to capture scroll-wheel zoom events.
    {
        HWND hwnd = static_cast<HWND>(window.nativeHandle());
        g_origWndProc = reinterpret_cast<WNDPROC>(
            GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(ZoomWndProc));
    }

    // ------------------------------------------------------------------
    // 3. Create GPU device
    // ------------------------------------------------------------------
    engine::rendering::GpuDevice device = engine::rendering::GpuDevice::create({
        .window = &window,
        .vsync  = true,
    });
    if (!device.isValid()) return EXIT_FAILURE;

    auto* d3dDevice = static_cast<ID3D12Device*>(device.nativeDevice());

    // ------------------------------------------------------------------
    // 4. Upload mesh to GPU
    // MeshManager must be constructed while a frame is open (after beginFrame).
    // A dedicated upload frame is used so the copy commands finish before rendering.
    // ------------------------------------------------------------------
    device.beginFrame();
    engine::rendering::MeshManager meshMgr(device);
    engine::rendering::MeshHandle meshHandle = meshMgr.uploadStatic(
        std::span<const engine::rendering::VertexStatic>(cpuMesh.vertices),
        std::span<const uint32_t>(cpuMesh.indices));
    device.endFrame();
    device.flush();

    // ------------------------------------------------------------------
    // 5. Load compiled opaque shader bytecode from ./shaders/
    // ------------------------------------------------------------------
    std::vector<uint8_t> vsCode = loadCso(L"shaders\\OpaqueVS.cso");
    std::vector<uint8_t> psCode = loadCso(L"shaders\\OpaquePS.cso");
    if (vsCode.empty() || psCode.empty()) return EXIT_FAILURE;

    // ------------------------------------------------------------------
    // 6. Create PBR opaque pipeline (root signature + PSO)
    // ------------------------------------------------------------------
    engine::rendering::OpaquePassPipeline pipeline =
        engine::rendering::createOpaquePassPipeline(
            d3dDevice,
            vsCode.data(), vsCode.size(),
            psCode.data(), psCode.size(),
            87u,   // DXGI_FORMAT_R8G8B8A8_UNORM
            20u);  // DXGI_FORMAT_D32_FLOAT

    // ------------------------------------------------------------------
    // 7. Upload-heap constant / structured buffers — persistently mapped.
    // perFrameBuf  : 512 bytes  PerFrameConstants CBV (b0)
    // perObjectBuf : 256 bytes  PerObjectConstants CBV (b1)
    // materialsBuf :  64 bytes  GpuMaterial structured buffer root SRV (t0, space0)
    // lightsBuf    :  64 bytes  GpuLight structured buffer root SRV (t1, space0)
    // ------------------------------------------------------------------
    using Microsoft::WRL::ComPtr;
    ComPtr<ID3D12Resource> perFrameBuf, perObjectBuf, materialsBuf, lightsBuf;
    if (!createUploadBuffer(d3dDevice, 512, perFrameBuf))  return EXIT_FAILURE;
    if (!createUploadBuffer(d3dDevice, 256, perObjectBuf)) return EXIT_FAILURE;
    if (!createUploadBuffer(d3dDevice,  64, materialsBuf)) return EXIT_FAILURE;
    if (!createUploadBuffer(d3dDevice,  64, lightsBuf))    return EXIT_FAILURE;

    void* perFramePtr  = nullptr;
    void* perObjectPtr = nullptr;
    void* materialsPtr = nullptr;
    void* lightsPtr    = nullptr;
    perFrameBuf ->Map(0, nullptr, &perFramePtr);
    perObjectBuf->Map(0, nullptr, &perObjectPtr);
    materialsBuf->Map(0, nullptr, &materialsPtr);
    lightsBuf   ->Map(0, nullptr, &lightsPtr);

    // Default material: plain white, metallic=0, roughness=0.5, no textures.
    {
        engine::rendering::GpuMaterial mat = {};
        mat.albedoTextureIndex     = 0xFFFFFFFFu;
        mat.normalTextureIndex     = 0xFFFFFFFFu;
        mat.metallicRoughnessIndex = 0xFFFFFFFFu;
        mat.emissiveTextureIndex   = 0xFFFFFFFFu;
        mat.albedoFactor[0]        = 0.8f;
        mat.albedoFactor[1]        = 0.8f;
        mat.albedoFactor[2]        = 0.8f;
        mat.albedoFactor[3]        = 1.0f;
        mat.metallicFactor         = 0.0f;
        mat.roughnessFactor        = 0.5f;
        std::memcpy(materialsPtr, &mat, sizeof(mat));
    }

    // Single directional light: direction normalize(1,1,-1), intensity 1.5.
    // direction.xyz points toward the light source (used as L in the shader).
    {
        static constexpr float kInvSqrt3 = 0.57735026919f;
        struct GpuLightCpu {
            float position[4];
            float direction[4];
            float color[4];
            float spotAngles[4];
        } light = {};
        light.position[3]   = 0.0f;         // type = 0 (directional)
        light.direction[0]  = kInvSqrt3;
        light.direction[1]  = kInvSqrt3;
        light.direction[2]  = -kInvSqrt3;
        light.direction[3]  = 100.0f;       // range (unused for directional)
        light.color[0]      = 1.5f;         // color * intensity (linear)
        light.color[1]      = 1.5f;
        light.color[2]      = 1.5f;
        light.color[3]      = 1.0f;
        light.spotAngles[2] = -1.0f;        // shadow map index -1 = no shadow
        std::memcpy(lightsPtr, &light, sizeof(light));
    }

    // ------------------------------------------------------------------
    // 8. Descriptor heap with 6 stub null SRVs for params 3 and 5.
    //   [0]    param 3 (bindless textures): null Texture2D  (t1+, space1)
    //   [1..5] param 5 (IBL + shadow):
    //          [1] gIrradianceMap  — null TextureCube
    //          [2] gPrefilteredEnv — null TextureCube
    //          [3] gBrdfLut        — null Texture2D
    //          [4] gShadowCascades — null Texture2DArray
    //          [5] gShadowSpots    — null Texture2DArray
    // hasIbl=0 and hasShadows=0 so the shader never samples these slots.
    // ------------------------------------------------------------------
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 6;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(d3dDevice->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap))))
            return EXIT_FAILURE;
    }

    const UINT srvInc =
        d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const SIZE_T cpuBase = srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    auto cpuH = [&](UINT i) -> D3D12_CPU_DESCRIPTOR_HANDLE {
        return { cpuBase + static_cast<SIZE_T>(i) * srvInc };
    };

    // [0] null Texture2D for bindless texture table (param 3)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MipLevels     = 1;
        d3dDevice->CreateShaderResourceView(nullptr, &s, cpuH(0));
    }
    // [1] null TextureCube for gIrradianceMap
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
        s.TextureCube.MipLevels     = 1;
        d3dDevice->CreateShaderResourceView(nullptr, &s, cpuH(1));
    }
    // [2] null TextureCube for gPrefilteredEnv
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
        s.TextureCube.MipLevels     = 1;
        d3dDevice->CreateShaderResourceView(nullptr, &s, cpuH(2));
    }
    // [3] null Texture2D for gBrdfLut
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MipLevels     = 1;
        d3dDevice->CreateShaderResourceView(nullptr, &s, cpuH(3));
    }
    // [4] null Texture2DArray for gShadowCascades
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MipLevels        = 1;
        s.Texture2DArray.ArraySize        = 1;
        d3dDevice->CreateShaderResourceView(nullptr, &s, cpuH(4));
    }
    // [5] null Texture2DArray for gShadowSpots
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MipLevels        = 1;
        s.Texture2DArray.ArraySize        = 1;
        d3dDevice->CreateShaderResourceView(nullptr, &s, cpuH(5));
    }

    // ------------------------------------------------------------------
    // 9. Wire FrameGraph to device
    // ------------------------------------------------------------------
    engine::rendering::FrameGraph fg;
    engine::rendering::frameGraphSetDevice(fg, device.nativeDevice());

    // ------------------------------------------------------------------
    // 10. Camera setup
    //     Position: (0, 1.5, 4), identity rotation, fovY = 60°
    // ------------------------------------------------------------------
    engine::core::math::Transform camTransform{};
    camTransform.position = { 0.0f, 1.5f, 4.0f };
    camTransform.rotation = engine::core::math::Quat::identity();
    camTransform.scale    = { 1.0f, 1.0f, 1.0f };

    engine::rendering::Camera cam{};
    cam.fovYDegrees = 60.0f;
    cam.nearZ       = 0.1f;
    cam.farZ        = 100.0f;
    cam.isMain      = true;

    // ------------------------------------------------------------------
    // 11. QPC timer
    // ------------------------------------------------------------------
    LARGE_INTEGER qpcFreq, qpcLast;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcLast);

    float angleRad = 0.0f;
    float cameraZ  = 4.0f;

    // Pre-compute GPU virtual addresses and descriptor handles (stable across frames).
    const D3D12_GPU_VIRTUAL_ADDRESS perFrameVA  = perFrameBuf ->GetGPUVirtualAddress();
    const D3D12_GPU_VIRTUAL_ADDRESS perObjectVA = perObjectBuf->GetGPUVirtualAddress();
    const D3D12_GPU_VIRTUAL_ADDRESS materialsVA = materialsBuf->GetGPUVirtualAddress();
    const D3D12_GPU_VIRTUAL_ADDRESS lightsVA    = lightsBuf   ->GetGPUVirtualAddress();

    const UINT64 gpuBase = srvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
    const D3D12_GPU_DESCRIPTOR_HANDLE bindlessGpu   = { gpuBase };
    const D3D12_GPU_DESCRIPTOR_HANDLE iblShadowGpu  = { gpuBase + srvInc };

    // ------------------------------------------------------------------
    // 12. Main loop
    // ------------------------------------------------------------------
    while (!window.wantsClose()) {
        // Compute dt
        LARGE_INTEGER qpcNow;
        QueryPerformanceCounter(&qpcNow);
        const float dt = clampf(
            static_cast<float>(qpcNow.QuadPart - qpcLast.QuadPart) /
            static_cast<float>(qpcFreq.QuadPart),
            0.0f, 0.1f);
        qpcLast = qpcNow;

        // Scroll-wheel zoom
        if (g_wheelAccum != 0) {
            cameraZ -= (g_wheelAccum / static_cast<float>(WHEEL_DELTA)) * 0.5f;
            cameraZ  = clampf(cameraZ, 0.5f, 50.0f);
            g_wheelAccum = 0;
        }
        camTransform.position = { 0.0f, 1.5f, cameraZ };

        angleRad += dt * 1.0f;  // 1 rad/s around Y

        // Build matrices (row-major, left-to-right convention: v' = v * M)
        const engine::core::math::Mat4 world =
            engine::core::math::toMat4(
                engine::core::math::fromAxisAngle(
                    engine::core::math::Vec3{ 0.0f, 1.0f, 0.0f }, angleRad));

        const engine::core::math::Mat4 view =
            engine::rendering::cameraViewMatrix(camTransform);

        const engine::core::math::Mat4 proj =
            engine::rendering::cameraProjMatrix(cam,
                static_cast<float>(kWidth) / static_cast<float>(kHeight));

        const engine::core::math::Mat4 viewProj = view * proj;

        // Fill PerFrameData and copy to upload buffer.
        {
            PerFrameData pf = {};
            std::memcpy(pf.viewMatrix,     &view.m[0][0],     sizeof(pf.viewMatrix));
            std::memcpy(pf.projMatrix,     &proj.m[0][0],     sizeof(pf.projMatrix));
            std::memcpy(pf.viewProjMatrix, &viewProj.m[0][0], sizeof(pf.viewProjMatrix));
            pf.cameraWorldPos[0] = camTransform.position.x;
            pf.cameraWorldPos[1] = camTransform.position.y;
            pf.cameraWorldPos[2] = camTransform.position.z;
            pf.time              = angleRad;
            pf.lightCount        = 1;
            pf.hasIbl            = 0;
            pf.hasShadows        = 0;
            // cascadeSplits not used (hasShadows=0); set to large values so no cascade matches.
            pf.cascadeSplits[0] = 1.0e30f;
            pf.cascadeSplits[1] = 1.0e30f;
            pf.cascadeSplits[2] = 1.0e30f;
            pf.cascadeSplits[3] = 1.0e30f;
            std::memcpy(perFramePtr, &pf, sizeof(pf));
        }

        // Fill PerObjectData and copy to upload buffer.
        {
            PerObjectData po = {};
            std::memcpy(po.worldMatrix, &world.m[0][0], sizeof(po.worldMatrix));
            po.materialIndex = 0;
            std::memcpy(perObjectPtr, &po, sizeof(po));
        }

        // Render
        device.beginFrame();
        fg.reset();

        // Import the back buffer (already in RENDER_TARGET state after beginFrame).
        const engine::rendering::ResourceHandle bb =
            fg.importBackBuffer(
                device.nativeBackBuffer(),
                device.currentBackBufferRtvHandle(),
                static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));

        // Import the depth buffer (always DEPTH_WRITE).
        const engine::rendering::ResourceHandle db =
            fg.importDepthBuffer(
                device.nativeDepthBuffer(),
                device.depthBufferDsvHandle());

        // Add the PBR opaque pass.
        fg.addPass(
            "OpaquePass",
            [bb, db](engine::rendering::FrameGraph::PassBuilder& b) {
                b.write(bb, static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
                b.read (db, static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
            },
            [&pipeline, &meshMgr, meshHandle, bb, db,
             srvHeapPtr    = srvHeap.Get(),
             bindlessGpu, iblShadowGpu,
             perFrameVA, perObjectVA, materialsVA, lightsVA]
            (void* cmdListVoid, const engine::rendering::PassResources& res)
            {
                auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);

                // Bind render target + depth buffer.
                D3D12_CPU_DESCRIPTOR_HANDLE rtv{ res.getRtvHandle(bb) };
                D3D12_CPU_DESCRIPTOR_HANDLE dsv{ res.getDsvHandle(db) };
                cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

                static constexpr float kClearColor[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
                cmd->ClearRenderTargetView(rtv, kClearColor, 0, nullptr);
                // Reverse-Z: clear depth to 0.0f (= far plane).
                cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

                // Viewport + scissor.
                engine::rendering::setFullscreenViewportScissor(cmdListVoid, kWidth, kHeight);

                // Bind shader-visible descriptor heap before SetGraphicsRootDescriptorTable.
                ID3D12DescriptorHeap* heaps[] = { srvHeapPtr };
                cmd->SetDescriptorHeaps(1, heaps);

                // Root signature and PSO.
                cmd->SetGraphicsRootSignature(pipeline.rootSignature.Get());
                cmd->SetPipelineState(pipeline.pso.Get());

                // Root parameter bindings (see PipelineState.h for layout):
                //   Param 0: per-frame CBV  b0
                //   Param 1: per-object CBV b1
                //   Param 2: materials SRV  t0, space0
                //   Param 3: bindless texture table  t1+, space1
                //   Param 4: lights SRV  t1, space0
                //   Param 5: IBL + shadow descriptor table  t2..t6, space0
                cmd->SetGraphicsRootConstantBufferView(0, perFrameVA);
                cmd->SetGraphicsRootConstantBufferView(1, perObjectVA);
                cmd->SetGraphicsRootShaderResourceView(2, materialsVA);
                cmd->SetGraphicsRootDescriptorTable(3, bindlessGpu);
                cmd->SetGraphicsRootShaderResourceView(4, lightsVA);
                cmd->SetGraphicsRootDescriptorTable(5, iblShadowGpu);

                // Bind mesh buffers and draw.
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmd->IASetVertexBuffers(0, 1,
                    static_cast<const D3D12_VERTEX_BUFFER_VIEW*>(
                        meshMgr.vertexBufferView(meshHandle)));
                cmd->IASetIndexBuffer(
                    static_cast<const D3D12_INDEX_BUFFER_VIEW*>(
                        meshMgr.indexBufferView(meshHandle)));
                cmd->DrawIndexedInstanced(meshMgr.indexCount(meshHandle), 1, 0, 0, 0);
            }
        );

        fg.compile();
        fg.execute(device.nativeCommandList());
        device.endFrame();
    }

    // ------------------------------------------------------------------
    // 13. Shutdown
    // ------------------------------------------------------------------
    device.flush();
    perFrameBuf ->Unmap(0, nullptr);
    perObjectBuf->Unmap(0, nullptr);
    materialsBuf->Unmap(0, nullptr);
    lightsBuf   ->Unmap(0, nullptr);

    std::filesystem::remove(eassetPath);
    return EXIT_SUCCESS;
}
