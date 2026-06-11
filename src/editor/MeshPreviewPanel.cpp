#ifdef ENGINE_DEVREL

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>

#include "editor/MeshPreviewPanel.h"

#include <rendering/Material.h>
#include <tools/EassetLoader.h>
#include <core/math/Mat.h>
#include <core/math/Vec.h>
#include <core/diag/Assert.h>

#include <PipelineState.h>
#include <FrameGraphImpl.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace engine::editor {

using Microsoft::WRL::ComPtr;

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
                        ComPtr<ID3D12Resource>& out)
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

MeshPreviewPanel::MeshPreviewPanel() = default;

MeshPreviewPanel::~MeshPreviewPanel() {
    if (initialized_ && device_) {
        device_->flush();
    }
}

void MeshPreviewPanel::init(rendering::GpuDevice& device,
                             uint64_t srvCpuHandle,
                             uint64_t srvGpuHandle)
{
    ENGINE_ASSERT(!initialized_, "MeshPreviewPanel::init called twice");
    device_       = &device;
    srvCpuHandle_ = srvCpuHandle;
    srvGpuHandle_ = srvGpuHandle;

    auto* dev = static_cast<ID3D12Device*>(device.nativeDevice());

    buildRT();
    rendering::frameGraphSetDevice(fg_, dev);

    // Load shaders and create PBR opaque pipeline.
    auto vsCode = loadCso(L"shaders\\OpaqueVS.cso");
    auto psCode = loadCso(L"shaders\\OpaquePS.cso");
    ENGINE_ASSERT(!vsCode.empty() && !psCode.empty(),
        "MeshPreviewPanel: failed to load OpaqueVS/PS shaders");

    pipeline_ = std::make_unique<rendering::OpaquePassPipeline>(
        rendering::createOpaquePassPipeline(
            dev,
            vsCode.data(), vsCode.size(),
            psCode.data(), psCode.size(),
            static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM),
            static_cast<uint32_t>(DXGI_FORMAT_D32_FLOAT)));

    // Upload-heap constant + structured buffers (persistently mapped).
    ENGINE_ASSERT(createUploadBuffer(dev, 512, perFrameBuf_),
        "MeshPreviewPanel: perFrameBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, 256, perObjectBuf_),
        "MeshPreviewPanel: perObjectBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, 64, materialsBuf_),
        "MeshPreviewPanel: materialsBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, 64, lightsBuf_),
        "MeshPreviewPanel: lightsBuf alloc failed");

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

    // 6-slot shader-visible heap for null SRV stubs.
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 6;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ENGINE_ASSERT(SUCCEEDED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&nullSrvHeap_))),
            "MeshPreviewPanel: null SRV heap creation failed");
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

    updateSrv();
    initialized_ = true;
}

// ---------------------------------------------------------------------------

void MeshPreviewPanel::buildRT()
{
    auto* dev = static_cast<ID3D12Device*>(device_->nativeDevice());

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = kW;
        rd.Height           = kH;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc       = { 1, 0 };
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE cv{};
        cv.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        cv.Color[0] = 0.15f; cv.Color[1] = 0.15f; cv.Color[2] = 0.20f; cv.Color[3] = 1.0f;

        HRESULT hr = dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
            IID_PPV_ARGS(&colorRt_));
        ENGINE_ASSERT(SUCCEEDED(hr), "MeshPreviewPanel: failed to create color RT");
    }

    {
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = kW;
        rd.Height           = kH;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = 1;
        rd.Format           = DXGI_FORMAT_D32_FLOAT;
        rd.SampleDesc       = { 1, 0 };
        rd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv{};
        cv.Format             = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 0.0f;

        HRESULT hr = dev->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
            IID_PPV_ARGS(&depthRt_));
        ENGINE_ASSERT(SUCCEEDED(hr), "MeshPreviewPanel: failed to create depth RT");
    }

    if (!rtvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 1;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap_));
        ENGINE_ASSERT(SUCCEEDED(hr), "MeshPreviewPanel: failed to create RTV heap");
        rtvCpu_ = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    }
    dev->CreateRenderTargetView(colorRt_.Get(), nullptr, rtvCpu_);

    if (!dsvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = 1;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsvHeap_));
        ENGINE_ASSERT(SUCCEEDED(hr), "MeshPreviewPanel: failed to create DSV heap");
        dsvCpu_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    }
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format        = DXGI_FORMAT_D32_FLOAT;
        dd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dev->CreateDepthStencilView(depthRt_.Get(), &dd, dsvCpu_);
    }
}

void MeshPreviewPanel::updateSrv()
{
    auto* dev = static_cast<ID3D12Device*>(device_->nativeDevice());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels     = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{ srvCpuHandle_ };
    dev->CreateShaderResourceView(colorRt_.Get(), &srvDesc, cpu);
}

// ---------------------------------------------------------------------------

void MeshPreviewPanel::loadAsset(const std::filesystem::path& path)
{
    if (!meshManager_ || path == loadedPath_) return;

    loadedPath_ = path;
    hasMesh_    = false;
    meshHandle_ = rendering::MeshHandle{};

    auto cpuMesh = tools::loadEasset(path);
    if (!cpuMesh || cpuMesh->vertices.empty()) return;

    meshHandle_ = meshManager_->uploadStatic(
        std::span<const rendering::VertexStatic>(cpuMesh->vertices),
        std::span<const uint32_t>(cpuMesh->indices));

    float maxDist = 0.0f;
    for (const auto& v : cpuMesh->vertices) {
        const float d = std::sqrt(v.position[0] * v.position[0]
                                + v.position[1] * v.position[1]
                                + v.position[2] * v.position[2]);
        maxDist = std::max(maxDist, d);
    }
    radius_ = defaultRadius_ = maxDist * 2.5f + 0.5f;

    hasMesh_ = meshHandle_.isValid();
}

// ---------------------------------------------------------------------------

void MeshPreviewPanel::render(void* cmdListVoid)
{
    if (!initialized_) return;

    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);

    fg_.reset();

    const rendering::ResourceHandle colorHandle = fg_.importBackBuffer(
        colorRt_.Get(),
        rtvCpu_.ptr,
        static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));

    const rendering::ResourceHandle depthHandle = fg_.importDepthBuffer(
        depthRt_.Get(),
        dsvCpu_.ptr);

    if (hasMesh_ && meshManager_ && pipeline_) {
        const float cx = radius_ * std::cos(pitch_) * std::sin(yaw_);
        const float cy = radius_ * std::sin(pitch_);
        const float cz = radius_ * std::cos(pitch_) * std::cos(yaw_);

        const core::math::Vec3 eye    = { cx, cy, cz };
        const core::math::Vec3 target = { 0.0f, 0.0f, 0.0f };

        const core::math::Mat4 view     = core::math::lookAtRh(eye, target, { 0.0f, 1.0f, 0.0f });
        const core::math::Mat4 proj     = core::math::perspectiveRhYupReverseZ(
            kPi / 3.0f, 1.0f, 0.05f, 500.0f);
        const core::math::Mat4 viewProj = view * proj;

        {
            PerFrameData pf = {};
            std::memcpy(pf.viewMatrix,     &view.m[0][0],     sizeof(pf.viewMatrix));
            std::memcpy(pf.projMatrix,     &proj.m[0][0],     sizeof(pf.projMatrix));
            std::memcpy(pf.viewProjMatrix, &viewProj.m[0][0], sizeof(pf.viewProjMatrix));
            pf.cameraWorldPos[0] = cx;
            pf.cameraWorldPos[1] = cy;
            pf.cameraWorldPos[2] = cz;
            pf.lightCount  = 1;
            pf.hasIbl      = 0;
            pf.hasShadows  = 0;
            pf.cascadeSplits[0] = pf.cascadeSplits[1] =
            pf.cascadeSplits[2] = pf.cascadeSplits[3] = 1.0e30f;
            std::memcpy(perFramePtr_, &pf, sizeof(pf));
        }

        {
            PerObjectData po = {};
            po.worldMatrix[0] = po.worldMatrix[5] = po.worldMatrix[10] = po.worldMatrix[15] = 1.0f;
            po.materialIndex  = 0;
            std::memcpy(perObjectPtr_, &po, sizeof(po));
        }

        fg_.addPass(
            "PreviewOpaque",
            [colorHandle, depthHandle](rendering::FrameGraph::PassBuilder& b) {
                b.write(colorHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
                b.read (depthHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
            },
            [this, colorHandle, depthHandle, meshHandle = meshHandle_]
            (void* cl, const rendering::PassResources& res) {
                auto* c = static_cast<ID3D12GraphicsCommandList*>(cl);

                D3D12_CPU_DESCRIPTOR_HANDLE rtv{ res.getRtvHandle(colorHandle) };
                D3D12_CPU_DESCRIPTOR_HANDLE dsv{ res.getDsvHandle(depthHandle) };
                c->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

                static constexpr float kClear[4] = { 0.15f, 0.15f, 0.20f, 1.0f };
                c->ClearRenderTargetView(rtv, kClear, 0, nullptr);
                c->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

                ID3D12DescriptorHeap* heaps[] = { nullSrvHeap_.Get() };
                c->SetDescriptorHeaps(1, heaps);

                c->SetGraphicsRootSignature(pipeline_->rootSignature.Get());
                c->SetPipelineState(pipeline_->pso.Get());

                c->SetGraphicsRootConstantBufferView(0, perFrameBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootConstantBufferView(1, perObjectBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootShaderResourceView(2, materialsBuf_->GetGPUVirtualAddress());

                const UINT64 gpuBase = nullSrvHeap_->GetGPUDescriptorHandleForHeapStart().ptr;
                c->SetGraphicsRootDescriptorTable(3, { gpuBase });
                c->SetGraphicsRootShaderResourceView(4, lightsBuf_->GetGPUVirtualAddress());
                c->SetGraphicsRootDescriptorTable(5, { gpuBase + srvDescSize_ });

                rendering::setFullscreenViewportScissor(cl, kW, kH);
                c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                c->IASetVertexBuffers(0, 1,
                    static_cast<const D3D12_VERTEX_BUFFER_VIEW*>(
                        meshManager_->vertexBufferView(meshHandle)));
                c->IASetIndexBuffer(
                    static_cast<const D3D12_INDEX_BUFFER_VIEW*>(
                        meshManager_->indexBufferView(meshHandle)));
                c->DrawIndexedInstanced(meshManager_->indexCount(meshHandle), 1, 0, 0, 0);
            });
    } else {
        fg_.addPass(
            "PreviewClear",
            [colorHandle, depthHandle](rendering::FrameGraph::PassBuilder& b) {
                b.write(colorHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
                b.read (depthHandle, static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
            },
            [this](void* cl, const rendering::PassResources& res) {
                (void)res;
                auto* c = static_cast<ID3D12GraphicsCommandList*>(cl);
                D3D12_CPU_DESCRIPTOR_HANDLE rtv{ rtvCpu_ };
                static constexpr float kClear[4] = { 0.15f, 0.15f, 0.20f, 1.0f };
                c->ClearRenderTargetView(rtv, kClear, 0, nullptr);
            });
    }

    fg_.compile();
    fg_.execute(cmd);

    D3D12_RESOURCE_BARRIER toSrv{};
    toSrv.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toSrv.Transition.pResource   = colorRt_.Get();
    toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toSrv.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &toSrv);
    rtInSrvState_ = true;
}

void MeshPreviewPanel::postFrameBarrier(void* cmdListVoid)
{
    if (!initialized_ || !rtInSrvState_) return;
    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);

    D3D12_RESOURCE_BARRIER toRt{};
    toRt.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRt.Transition.pResource   = colorRt_.Get();
    toRt.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toRt.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRt.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &toRt);
    rtInSrvState_ = false;
}

// ---------------------------------------------------------------------------

void MeshPreviewPanel::draw(bool* open)
{
    if (!initialized_) return;

    ImGui::SetNextWindowSize(ImVec2(300, 340), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Asset Preview", open)) {
        ImGui::End();
        return;
    }

    if (hasMesh_) {
        ImGui::Text("Orbit: left-drag   Zoom: scroll");
        ImGui::SameLine();
        if (ImGui::SmallButton("Recenter")) {
            yaw_    = 0.7854f;
            pitch_  = 0.4363f;
            radius_ = defaultRadius_;
        }
    } else {
        ImGui::TextDisabled("Select an .easset in the browser");
    }

    const ImVec2 imagePos  = ImGui::GetCursorScreenPos();
    const ImVec2 imageSize = { static_cast<float>(kW), static_cast<float>(kH) };

    ImGui::InvisibleButton("##orbit", imageSize, ImGuiButtonFlags_MouseButtonLeft);

    if (srvGpuHandle_ != 0) {
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(srvGpuHandle_),
            imagePos,
            { imagePos.x + imageSize.x, imagePos.y + imageSize.y });
    }

    if (ImGui::IsItemActive()) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        yaw_   -= delta.x * 0.008f;
        pitch_ += delta.y * 0.008f;
        pitch_  = std::max(-1.48f, std::min(1.48f, pitch_));
    }

    if (ImGui::IsItemHovered()) {
        const float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f) {
            radius_ *= (1.0f - scroll * 0.15f);
            radius_  = std::max(0.1f, std::min(500.0f, radius_));
        }
    }

    if (hasMesh_ && !loadedPath_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", loadedPath_.filename().string().c_str());
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------

void MeshPreviewPanel::drawInline(ImVec2 size)
{
    if (!initialized_) return;

    if (!hasMesh_) {
        ImGui::TextDisabled("Select an .easset in the browser");
        return;
    }

    if (size.x < 1.0f) size.x = 1.0f;
    if (size.y < 1.0f) size.y = 1.0f;

    ImGui::Text("Orbit: left-drag   Zoom: scroll");
    ImGui::SameLine();
    if (ImGui::SmallButton("Recenter")) {
        yaw_    = 0.7854f;
        pitch_  = 0.4363f;
        radius_ = defaultRadius_;
    }

    const float controlsHeight = ImGui::GetFrameHeightWithSpacing();
    const float imgH = size.y - controlsHeight;
    const ImVec2 imageSize = { size.x, imgH > 1.0f ? imgH : 1.0f };

    const ImVec2 imagePos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##orbit_inline", imageSize, ImGuiButtonFlags_MouseButtonLeft);

    if (srvGpuHandle_ != 0) {
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(srvGpuHandle_),
            imagePos,
            { imagePos.x + imageSize.x, imagePos.y + imageSize.y });
    }

    if (ImGui::IsItemActive()) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        yaw_   -= delta.x * 0.008f;
        pitch_ += delta.y * 0.008f;
        pitch_  = std::max(-1.48f, std::min(1.48f, pitch_));
    }

    if (ImGui::IsItemHovered()) {
        const float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f) {
            radius_ *= (1.0f - scroll * 0.15f);
            radius_  = std::max(0.1f, std::min(500.0f, radius_));
        }
    }

    if (!loadedPath_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", loadedPath_.filename().string().c_str());
    }
}

} // namespace engine::editor

#endif // ENGINE_DEVREL
