#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "editor/ThumbnailRenderer.h"

#include <rendering/GpuDevice.h>
#include <rendering/MeshManager.h>
#include <rendering/FrameGraph.h>
#include <rendering/Material.h>
#include <tools/EassetLoader.h>
#include <core/diag/Assert.h>
#include <core/log.h>
#include <core/math/Mat.h>
#include <core/math/Vec.h>

#include <PipelineState.h>
#include <FrameGraphImpl.h>

#include <span>
#include <cmath>
#include <cstring>
#include <vector>

namespace engine::editor {

namespace {

static constexpr float kPi = 3.14159265358979f;

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
    "PerFrameData must match HLSL PerFrameConstants");

struct PerObjectData {
    float    worldMatrix[16];
    uint32_t materialIndex;
    float    pad[3];
};
static_assert(sizeof(PerObjectData) == 80,
    "PerObjectData must match HLSL PerObjectConstants");

bool createUploadBuffer(ID3D12Device* dev, UINT64 bytes,
                        Microsoft::WRL::ComPtr<ID3D12Resource>& out)
{
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

std::vector<uint8_t> loadCso(const wchar_t* relPath)
{
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

} // anonymous namespace

// ---------------------------------------------------------------------------

ThumbnailRenderer::ThumbnailRenderer() = default;

ThumbnailRenderer::~ThumbnailRenderer() {
    if (initialized_ && device_) {
        device_->flush();
    }
}

void ThumbnailRenderer::init(rendering::GpuDevice& device, SrvAllocFn srvAlloc) {
    ENGINE_ASSERT(!initialized_, "ThumbnailRenderer::init called twice");
    device_   = &device;
    srvAlloc_ = std::move(srvAlloc);

    auto* dev = static_cast<ID3D12Device*>(device.nativeDevice());

    rendering::frameGraphSetDevice(thumbFg_, dev);

    auto vsCode = loadCso(L"shaders\\OpaqueVS.cso");
    auto psCode = loadCso(L"shaders\\OpaquePS.cso");
    ENGINE_ASSERT(!vsCode.empty() && !psCode.empty(),
        "ThumbnailRenderer: failed to load OpaqueVS/PS shaders");

    pipeline_ = std::make_unique<rendering::OpaquePassPipeline>(
        rendering::createOpaquePassPipeline(
            dev,
            vsCode.data(), vsCode.size(),
            psCode.data(), psCode.size(),
            static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM),
            static_cast<uint32_t>(DXGI_FORMAT_D32_FLOAT)));

    // Upload-heap constant + structured buffers (persistently mapped).
    // All thumbnails use the same isometric camera and identity world matrix,
    // so a single perFrame/perObject slot suffices.
    ENGINE_ASSERT(createUploadBuffer(dev, 512, perFrameBuf_),
        "ThumbnailRenderer: perFrameBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, 256, perObjectBuf_),
        "ThumbnailRenderer: perObjectBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, 64, materialsBuf_),
        "ThumbnailRenderer: materialsBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, 64, lightsBuf_),
        "ThumbnailRenderer: lightsBuf alloc failed");

    perFrameBuf_ ->Map(0, nullptr, &perFramePtr_);
    perObjectBuf_->Map(0, nullptr, &perObjectPtr_);

    // Default material: white, roughness=0.5, no textures.
    {
        rendering::GpuMaterial mat = {};
        mat.albedoTextureIndex     = 0xFFFFFFFFu;
        mat.normalTextureIndex     = 0xFFFFFFFFu;
        mat.metallicRoughnessIndex = 0xFFFFFFFFu;
        mat.emissiveTextureIndex   = 0xFFFFFFFFu;
        mat.albedoFactor[0] = 0.8f; mat.albedoFactor[1] = 0.8f;
        mat.albedoFactor[2] = 0.8f; mat.albedoFactor[3] = 1.0f;
        mat.metallicFactor  = 0.0f;
        mat.roughnessFactor = 0.5f;
        void* ptr = nullptr;
        materialsBuf_->Map(0, nullptr, &ptr);
        std::memcpy(ptr, &mat, sizeof(mat));
        materialsBuf_->Unmap(0, nullptr);
    }

    // Single directional light: direction normalize(1,1,-1).
    {
        static constexpr float kInvSqrt3 = 0.57735026919f;
        struct GpuLightCpu {
            float position[4]; float direction[4]; float color[4]; float spotAngles[4];
        } light = {};
        light.position[3]   = 0.0f;
        light.direction[0]  = kInvSqrt3;
        light.direction[1]  = kInvSqrt3;
        light.direction[2]  = -kInvSqrt3;
        light.direction[3]  = 100.0f;
        light.color[0]      = 1.5f;
        light.color[1]      = 1.5f;
        light.color[2]      = 1.5f;
        light.color[3]      = 1.0f;
        light.spotAngles[2] = -1.0f;
        void* ptr = nullptr;
        lightsBuf_->Map(0, nullptr, &ptr);
        std::memcpy(ptr, &light, sizeof(light));
        lightsBuf_->Unmap(0, nullptr);
    }

    // Identity PerObject (world = identity, materialIndex = 0) — static for all thumbnails.
    {
        PerObjectData po = {};
        po.worldMatrix[0] = po.worldMatrix[5] = po.worldMatrix[10] = po.worldMatrix[15] = 1.0f;
        po.materialIndex  = 0;
        std::memcpy(perObjectPtr_, &po, sizeof(po));
    }

    // Isometric PerFrame — identical for all thumbnails (same camera).
    {
        using namespace core::math;
        const Vec3 eye    = { 1.5f, 1.5f, 1.5f };
        const Vec3 target = { 0.0f, 0.0f, 0.0f };
        const Vec3 up     = { 0.0f, 1.0f, 0.0f };
        const Mat4 view   = lookAtRh(eye, target, up);
        const Mat4 proj   = perspectiveRhYupReverseZ(60.0f * (kPi / 180.0f), 1.0f, 0.1f, 100.0f);
        const Mat4 vp     = view * proj;

        PerFrameData pf = {};
        std::memcpy(pf.viewMatrix,     &view.m[0][0], sizeof(pf.viewMatrix));
        std::memcpy(pf.projMatrix,     &proj.m[0][0], sizeof(pf.projMatrix));
        std::memcpy(pf.viewProjMatrix, &vp.m[0][0],   sizeof(pf.viewProjMatrix));
        pf.cameraWorldPos[0] = 1.5f; pf.cameraWorldPos[1] = 1.5f; pf.cameraWorldPos[2] = 1.5f;
        pf.lightCount  = 1;
        pf.hasIbl      = 0;
        pf.hasShadows  = 0;
        pf.cascadeSplits[0] = pf.cascadeSplits[1] =
        pf.cascadeSplits[2] = pf.cascadeSplits[3] = 1.0e30f;
        std::memcpy(perFramePtr_, &pf, sizeof(pf));
    }

    // 6-slot shader-visible heap for null SRV stubs.
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 6;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ENGINE_ASSERT(SUCCEEDED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&nullSrvHeap_))),
            "ThumbnailRenderer: null SRV heap creation failed");
    }

    srvDescSize_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const SIZE_T cpuBase = nullSrvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;

    auto mkNull2D = [&](UINT i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MipLevels     = 1;
        dev->CreateShaderResourceView(nullptr, &s, { cpuBase + static_cast<SIZE_T>(i) * srvDescSize_ });
    };
    auto mkNullCube = [&](UINT i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
        s.TextureCube.MipLevels     = 1;
        dev->CreateShaderResourceView(nullptr, &s, { cpuBase + static_cast<SIZE_T>(i) * srvDescSize_ });
    };
    auto mkNullArr = [&](UINT i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MipLevels        = 1;
        s.Texture2DArray.ArraySize        = 1;
        dev->CreateShaderResourceView(nullptr, &s, { cpuBase + static_cast<SIZE_T>(i) * srvDescSize_ });
    };

    mkNull2D(0);
    mkNullCube(1);
    mkNullCube(2);
    mkNull2D(3);
    mkNullArr(4);
    mkNullArr(5);

    // RTV heap (up to 64 thumbnails).
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 64;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap_));
        ENGINE_ASSERT(SUCCEEDED(hr), "ThumbnailRenderer: failed to create RTV heap");
    }

    // DSV heap (up to 64 thumbnails).
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = 64;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap_));
        ENGINE_ASSERT(SUCCEEDED(hr), "ThumbnailRenderer: failed to create DSV heap");
    }

    initialized_ = true;
}

void ThumbnailRenderer::requestThumbnail(const std::filesystem::path& path) {
    const std::string key = path.string();
    if (cache_.count(key)) return;
    pending_.push_back(path);
    cache_.emplace(key, ThumbnailEntry{});
}

void ThumbnailRenderer::flushPending(void* cmdListVoid) {
    if (!initialized_ || !meshManager_ || pending_.empty()) return;

    auto* cmdList  = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);
    auto* dev      = static_cast<ID3D12Device*>(device_->nativeDevice());

    const uint32_t rtvDescSize = dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    const uint32_t dsvDescSize = dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    constexpr uint32_t kThumbW = 64;
    constexpr uint32_t kThumbH = 64;

    const UINT64 gpuBase = nullSrvHeap_->GetGPUDescriptorHandleForHeapStart().ptr;

    for (const auto& path : pending_) {
        const std::string key = path.string();

        auto cpuMesh = tools::loadEasset(path);
        if (!cpuMesh) {
            LOG_WARN("ThumbnailRenderer: loadEasset failed for '{}'", key);
            continue;
        }

        rendering::MeshHandle gpuHandle = meshManager_->uploadStatic(
            std::span<const rendering::VertexStatic>(cpuMesh->vertices),
            std::span<const uint32_t>(cpuMesh->indices));

        // Create 64x64 color RT.
        Microsoft::WRL::ComPtr<ID3D12Resource> colorRt;
        {
            D3D12_HEAP_PROPERTIES hp = {};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC rd = {};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width            = kThumbW;
            rd.Height           = kThumbH;
            rd.DepthOrArraySize = 1;
            rd.MipLevels        = 1;
            rd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
            rd.SampleDesc       = { 1, 0 };
            rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            D3D12_CLEAR_VALUE cv = {};
            cv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            cv.Color[0] = 0.1f; cv.Color[1] = 0.1f; cv.Color[2] = 0.12f; cv.Color[3] = 1.0f;

            HRESULT hr = dev->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                IID_PPV_ARGS(&colorRt));
            if (FAILED(hr)) {
                LOG_WARN("ThumbnailRenderer: failed to create color RT for '{}'", key);
                continue;
            }
        }

        // Create 64x64 depth RT.
        Microsoft::WRL::ComPtr<ID3D12Resource> depthRt;
        {
            D3D12_HEAP_PROPERTIES hp = {};
            hp.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC rd = {};
            rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            rd.Width            = kThumbW;
            rd.Height           = kThumbH;
            rd.DepthOrArraySize = 1;
            rd.MipLevels        = 1;
            rd.Format           = DXGI_FORMAT_D32_FLOAT;
            rd.SampleDesc       = { 1, 0 };
            rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE cv = {};
            cv.Format             = DXGI_FORMAT_D32_FLOAT;
            cv.DepthStencil.Depth = 0.0f;

            HRESULT hr = dev->CreateCommittedResource(
                &hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                IID_PPV_ARGS(&depthRt));
            if (FAILED(hr)) {
                LOG_WARN("ThumbnailRenderer: failed to create depth RT for '{}'", key);
                continue;
            }
        }

        if (nextRtvSlot_ >= 64) {
            LOG_WARN("ThumbnailRenderer: RTV heap exhausted, skipping '{}'", key);
            continue;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        rtvCpu.ptr += static_cast<SIZE_T>(nextRtvSlot_) * rtvDescSize;
        ++nextRtvSlot_;

        dev->CreateRenderTargetView(colorRt.Get(), nullptr, rtvCpu);

        if (nextDsvSlot_ >= 64) {
            LOG_WARN("ThumbnailRenderer: DSV heap exhausted, skipping '{}'", key);
            continue;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsvCpu = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        dsvCpu.ptr += static_cast<SIZE_T>(nextDsvSlot_) * dsvDescSize;
        ++nextDsvSlot_;

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dev->CreateDepthStencilView(depthRt.Get(), &dsvDesc, dsvCpu);

        uint64_t srvCpuPtr = 0;
        uint64_t srvGpuPtr = 0;
        srvAlloc_(srvCpuPtr, srvGpuPtr);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels     = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle{ srvCpuPtr };
        dev->CreateShaderResourceView(colorRt.Get(), &srvDesc, srvCpuHandle);

        thumbFg_.reset();

        const rendering::ResourceHandle colorHandle = thumbFg_.importBackBuffer(
            colorRt.Get(),
            rtvCpu.ptr,
            static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
        const rendering::ResourceHandle depthHandle = thumbFg_.importDepthBuffer(
            depthRt.Get(),
            dsvCpu.ptr);

        thumbFg_.addPass(
            "ThumbnailOpaque",
            [colorHandle, depthHandle](rendering::FrameGraph::PassBuilder& b) {
                b.write(colorHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
                b.read (depthHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
            },
            [this, gpuBase, colorHandle, depthHandle, gpuHandle]
            (void* cl, const rendering::PassResources& res) {
                auto* c = static_cast<ID3D12GraphicsCommandList*>(cl);

                D3D12_CPU_DESCRIPTOR_HANDLE rtv{ res.getRtvHandle(colorHandle) };
                D3D12_CPU_DESCRIPTOR_HANDLE dsv{ res.getDsvHandle(depthHandle) };
                c->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

                static constexpr float kClear[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
                c->ClearRenderTargetView(rtv, kClear, 0, nullptr);
                c->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

                ID3D12DescriptorHeap* heaps[] = { nullSrvHeap_.Get() };
                c->SetDescriptorHeaps(1, heaps);

                c->SetGraphicsRootSignature(pipeline_->rootSignature.Get());
                c->SetPipelineState(pipeline_->pso.Get());

                c->SetGraphicsRootConstantBufferView(0, perFrameBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootConstantBufferView(1, perObjectBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootShaderResourceView(2, materialsBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootDescriptorTable(3, { gpuBase });
                c->SetGraphicsRootShaderResourceView(4, lightsBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootDescriptorTable(5, { gpuBase + srvDescSize_ });

                rendering::setFullscreenViewportScissor(cl, 64, 64);
                c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->IASetVertexBuffers(0, 1,
                    static_cast<const D3D12_VERTEX_BUFFER_VIEW*>(
                        meshManager_->vertexBufferView(gpuHandle)));
                c->IASetIndexBuffer(
                    static_cast<const D3D12_INDEX_BUFFER_VIEW*>(
                        meshManager_->indexBufferView(gpuHandle)));
                c->DrawIndexedInstanced(meshManager_->indexCount(gpuHandle), 1, 0, 0, 0);
            });

        thumbFg_.compile();
        thumbFg_.execute(cmdList);

        D3D12_RESOURCE_BARRIER toSrv = {};
        toSrv.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrv.Transition.pResource   = colorRt.Get();
        toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &toSrv);

        ThumbnailEntry& entry = cache_[key];
        entry.srvGpuHandle    = srvGpuPtr;
        entry.colorRt         = std::move(colorRt);
        entry.depthRt         = std::move(depthRt);
        entry.rendered        = true;
    }

    pending_.clear();
}

ImTextureID ThumbnailRenderer::getImGuiTexture(const std::filesystem::path& path) const {
    const auto it = cache_.find(path.string());
    if (it == cache_.end()) return ImTextureID{0};
    if (!it->second.rendered) return ImTextureID{0};
    return static_cast<ImTextureID>(it->second.srvGpuHandle);
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
