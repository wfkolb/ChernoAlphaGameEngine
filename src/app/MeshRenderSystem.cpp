#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "MeshRenderSystem.h"
#include <tools/Profiler.h>

#include <core/ecs/View.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/math/Mat.h>
#include <core/math/Transform.h>
#include <core/log.h>
#include <rendering/Material.h>

// Internal rendering header — app layer may access rendering internals for pipeline setup.
#include <PipelineState.h>
#include <FrameGraphImpl.h>
#include <LightCullSystem.h>

#include <d3d12.h>
#include <wrl/client.h>
#include <cstring>
#include <vector>

namespace engine::app {

namespace {

// CPU mirror of HLSL PerFrameConstants (512 bytes).
struct PerFrameData {
    float    viewMatrix[16];
    float    projMatrix[16];
    float    viewProjMatrix[16];
    float    cameraWorldPos[3];
    float    pad0;
    float    time;
    uint32_t lightCount;
    uint32_t hasIbl;
    uint32_t viewMode;      // 0=Lit, 1=Unlit — mirrors HLSL PerFrameConstants.viewMode
    float    shadowCascadeMat[4][16];
    float    cascadeSplits[4];
    uint32_t hasShadows;
    float    pad2[3];
};
static_assert(sizeof(PerFrameData) == 512,
    "PerFrameData must match HLSL PerFrameConstants");

// CPU mirror of HLSL PerObjectConstants (80 bytes).
struct PerObjectData {
    float    worldMatrix[16];
    uint32_t materialIndex;
    float    pad[3];
};
static_assert(sizeof(PerObjectData) == 80,
    "PerObjectData must match HLSL PerObjectConstants");

// Per-object CBV slots are 256-byte aligned (D3D12 requirement).
static constexpr UINT64 kPerObjectSlotSize = 256;
static constexpr uint32_t kMaxEntities     = 256;

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

struct MeshRenderSystem::Impl {
    using ComPtr = Microsoft::WRL::ComPtr<ID3D12Resource>;
    rendering::OpaquePassPipeline               pipeline;
    Microsoft::WRL::ComPtr<ID3D12Resource>      perFrameBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource>      perObjectBuf; // kMaxEntities * kPerObjectSlotSize
    Microsoft::WRL::ComPtr<ID3D12Resource>      lightsBuf;   // rendering::GpuLightData::kMaxLights * 64 bytes
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    void*    perFramePtr  = nullptr;
    void*    perObjectPtr = nullptr;
    void*    lightsPtr    = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS perFrameVA        = 0;
    D3D12_GPU_VIRTUAL_ADDRESS perObjectBaseVA   = 0;
    D3D12_GPU_VIRTUAL_ADDRESS lightsVA          = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE bindlessGpu     = {};
    D3D12_GPU_DESCRIPTOR_HANDLE iblShadowGpu    = {};
    // TX-3: MaterialManager owned externally; pointer set in init().
    rendering::MaterialManager* materialManager_ = nullptr;
    // TX-2: Bindless texture slots (0-5 = stubs, 6+ = real textures).
    uint32_t nextTextureSlot_ = 6;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textureResources_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textureUploadBuffers_;
};

MeshRenderSystem::MeshRenderSystem()  = default;
MeshRenderSystem::~MeshRenderSystem() = default;

void MeshRenderSystem::init(rendering::GpuDevice&       device,
                             rendering::MaterialManager& materialManager,
                             uint32_t backBufferFormat,
                             uint32_t depthFormat)
{
    auto* dev = static_cast<ID3D12Device*>(device.nativeDevice());

    auto vsCode = loadCso(L"shaders\\OpaqueVS.cso");
    auto psCode = loadCso(L"shaders\\OpaquePS.cso");
    if (vsCode.empty() || psCode.empty()) {
        LOG_WARN("MeshRenderSystem::init: shaders not found — rendering disabled");
        return;
    }

    auto imp = std::make_unique<Impl>();

    imp->pipeline = rendering::createOpaquePassPipeline(
        dev,
        vsCode.data(), vsCode.size(),
        psCode.data(), psCode.size(),
        backBufferFormat, depthFormat);

    // Two 512-byte slots: slot 0 = editor viewport, slot 1 = PIE window.
    // Both may be in the same command list, so they must not share a buffer region.
    ENGINE_ASSERT(createUploadBuffer(dev, 512 * 2, imp->perFrameBuf),
        "MeshRenderSystem: perFrameBuf alloc failed");
    ENGINE_ASSERT(createUploadBuffer(dev, kMaxEntities * kPerObjectSlotSize, imp->perObjectBuf),
        "MeshRenderSystem: perObjectBuf alloc failed");
    static constexpr UINT64 kLightsBufBytes =
        static_cast<UINT64>(rendering::GpuLightData::kMaxLights) * 64u;
    ENGINE_ASSERT(createUploadBuffer(dev, kLightsBufBytes, imp->lightsBuf),
        "MeshRenderSystem: lightsBuf alloc failed");

    imp->perFrameBuf ->Map(0, nullptr, &imp->perFramePtr);
    imp->perObjectBuf->Map(0, nullptr, &imp->perObjectPtr);
    imp->lightsBuf   ->Map(0, nullptr, &imp->lightsPtr);

    imp->materialManager_ = &materialManager;

    // 1024-slot shader-visible heap: [0]=bindless table stub, [1..5]=IBL+shadow stubs,
    // [6+]=real bindless textures uploaded via uploadTexture().
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = 1024;
        hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ENGINE_ASSERT(SUCCEEDED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&imp->srvHeap))),
            "MeshRenderSystem: srvHeap creation failed");
    }

    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const SIZE_T cpuBase = imp->srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;

    auto mkNull2D = [&](UINT i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MipLevels     = 1;
        dev->CreateShaderResourceView(nullptr, &s, { cpuBase + static_cast<SIZE_T>(i) * inc });
    };
    auto mkNullCube = [&](UINT i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURECUBE;
        s.TextureCube.MipLevels     = 1;
        dev->CreateShaderResourceView(nullptr, &s, { cpuBase + static_cast<SIZE_T>(i) * inc });
    };
    auto mkNullArr = [&](UINT i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        s.Format                          = DXGI_FORMAT_R8G8B8A8_UNORM;
        s.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MipLevels        = 1;
        s.Texture2DArray.ArraySize        = 1;
        dev->CreateShaderResourceView(nullptr, &s, { cpuBase + static_cast<SIZE_T>(i) * inc });
    };

    mkNull2D(0);    // bindless table stub
    mkNullCube(1);  // gIrradianceMap
    mkNullCube(2);  // gPrefilteredEnv
    mkNull2D(3);    // gBrdfLut
    mkNullArr(4);   // gShadowCascades
    mkNullArr(5);   // gShadowSpots

    const UINT64 gpuBase = imp->srvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
    imp->bindlessGpu   = { gpuBase };
    imp->iblShadowGpu  = { gpuBase + inc };

    imp->perFrameVA      = imp->perFrameBuf ->GetGPUVirtualAddress();
    imp->perObjectBaseVA = imp->perObjectBuf->GetGPUVirtualAddress();
    imp->lightsVA        = imp->lightsBuf   ->GetGPUVirtualAddress();

    impl_ = std::move(imp);
}

void MeshRenderSystem::tick(core::ecs::World&         world,
                             rendering::MeshManager&   meshManager,
                             rendering::FrameGraph&    fg,
                             const float               viewMat[16],
                             const float               projMat[16],
                             const float               viewProjMat[16],
                             rendering::ResourceHandle backBuffer,
                             rendering::ResourceHandle depthBuffer,
                             uint32_t                  width,
                             uint32_t                  height,
                             uint32_t                  frameSlot,
                             const float               cameraWorldPos[3],
                             ViewMode                  viewMode)
{
    PROFILE_SCOPE("MeshRenderSystem::tick");
    if (!impl_) return;

    // Collect draw items.
    struct DrawItem {
        rendering::MeshHandle handle;
        core::math::Mat4      worldMat;
        uint32_t              slot;
        uint32_t              materialIndex;
    };
    std::vector<DrawItem> draws;
    draws.reserve(handles_.size());

    uint32_t slot = 0;
    core::ecs::View<core::Transform, core::MeshHandle> view(world);
    for (auto [entity, transform, meshHandle] : view) {
        auto it = handles_.find(entity.index);
        if (it == handles_.end()) continue;
        if (!it->second.isValid()) continue;
        if (slot >= kMaxEntities) break;

        core::math::Transform mt{};
        const auto* hc = world.tryGet<core::ecs::HierarchyComponent>(entity);
        if (hc && hc->parent != core::ecs::kInvalidEntity) {
            const core::Transform wt = core::ecs::computeWorldTransform(world, entity);
            mt.position = wt.position;
            mt.rotation = wt.rotation;
            mt.scale    = wt.scale;
        } else {
            mt.position = transform.position;
            mt.rotation = transform.rotation;
            mt.scale    = transform.scale;
        }

        draws.push_back({ it->second, mt.toMatrix(), slot++, meshHandle.materialIndex });
    }

    if (draws.empty()) return;

    auto& imp = *impl_;

    // Build the per-frame light array from ECS entities with Transform + Light components.
    const rendering::GpuLightData ld = rendering::buildLightArray(world);
    std::memcpy(imp.lightsPtr, ld.bytes, static_cast<size_t>(ld.count) * 64u);

    // Fill PerFrameData into the slot owned by this render path.
    // Slot 0 = editor viewport, slot 1 = PIE window. Each slot is 512 bytes
    // (256-byte aligned), so both can coexist in the same command list submission.
    const UINT64 perFrameSlotOffset = static_cast<UINT64>(frameSlot) * 512;
    {
        PerFrameData pf = {};
        std::memcpy(pf.viewMatrix,     viewMat,     sizeof(pf.viewMatrix));
        std::memcpy(pf.projMatrix,     projMat,     sizeof(pf.projMatrix));
        std::memcpy(pf.viewProjMatrix, viewProjMat, sizeof(pf.viewProjMatrix));
        if (cameraWorldPos) {
            pf.cameraWorldPos[0] = cameraWorldPos[0];
            pf.cameraWorldPos[1] = cameraWorldPos[1];
            pf.cameraWorldPos[2] = cameraWorldPos[2];
        }
        pf.lightCount = ld.count;
        pf.hasIbl     = 0;
        pf.viewMode   = static_cast<uint32_t>(viewMode);
        pf.hasShadows = 0;
        pf.cascadeSplits[0] = pf.cascadeSplits[1] =
        pf.cascadeSplits[2] = pf.cascadeSplits[3] = 1.0e30f;
        std::memcpy(static_cast<uint8_t*>(imp.perFramePtr) + perFrameSlotOffset, &pf, sizeof(pf));
    }

    // Fill PerObjectData for each entity (256-byte aligned slots).
    {
        auto* base = static_cast<uint8_t*>(imp.perObjectPtr);
        for (const auto& d : draws) {
            PerObjectData po = {};
            std::memcpy(po.worldMatrix, &d.worldMat.m[0][0], sizeof(po.worldMatrix));
            po.materialIndex = d.materialIndex;
            std::memcpy(base + d.slot * kPerObjectSlotSize, &po, sizeof(po));
        }
    }

    fg.addPass(
        "OpaqueEntities",
        [backBuffer, depthBuffer](rendering::FrameGraph::PassBuilder& b) {
            b.write(backBuffer, static_cast<uint32_t>(D3D12_RESOURCE_STATE_RENDER_TARGET));
            b.read (depthBuffer, static_cast<uint32_t>(D3D12_RESOURCE_STATE_DEPTH_WRITE));
        },
        [draws       = std::move(draws),
         &meshManager,
         pipeline    = &imp.pipeline,
         srvHeapPtr  = imp.srvHeap.Get(),
         bindlessGpu = imp.bindlessGpu,
         iblShadow   = imp.iblShadowGpu,
         perFrameVA  = imp.perFrameVA + perFrameSlotOffset,
         perObjBase  = imp.perObjectBaseVA,
         materialsVA = imp.materialManager_->gpuVirtualAddress(),
         lightsVA    = imp.lightsVA,
         backBuffer, depthBuffer, width, height]
        (void* cl, const rendering::PassResources& res)
        {
            auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cl);

            D3D12_CPU_DESCRIPTOR_HANDLE rtv{ res.getRtvHandle(backBuffer) };
            D3D12_CPU_DESCRIPTOR_HANDLE dsv{ res.getDsvHandle(depthBuffer) };
            cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

            ID3D12DescriptorHeap* heaps[] = { srvHeapPtr };
            cmd->SetDescriptorHeaps(1, heaps);

            cmd->SetGraphicsRootSignature(pipeline->rootSignature.Get());
            cmd->SetPipelineState(pipeline->pso.Get());

            cmd->SetGraphicsRootConstantBufferView(0, perFrameVA);
            cmd->SetGraphicsRootShaderResourceView(2, materialsVA);
            cmd->SetGraphicsRootDescriptorTable(3, bindlessGpu);
            cmd->SetGraphicsRootShaderResourceView(4, lightsVA);
            cmd->SetGraphicsRootDescriptorTable(5, iblShadow);

            rendering::setFullscreenViewportScissor(cl, width, height);
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            for (const auto& d : draws) {
                cmd->SetGraphicsRootConstantBufferView(
                    1, perObjBase + static_cast<UINT64>(d.slot) * kPerObjectSlotSize);
                cmd->IASetVertexBuffers(0, 1,
                    static_cast<const D3D12_VERTEX_BUFFER_VIEW*>(
                        meshManager.vertexBufferView(d.handle)));
                cmd->IASetIndexBuffer(
                    static_cast<const D3D12_INDEX_BUFFER_VIEW*>(
                        meshManager.indexBufferView(d.handle)));
                cmd->DrawIndexedInstanced(meshManager.indexCount(d.handle), 1, 0, 0, 0);
            }
        }
    );
}

void MeshRenderSystem::registerHandle(uint32_t entityIndex,
                                       rendering::MeshHandle gpuHandle)
{
    handles_[entityIndex] = gpuHandle;
}

void MeshRenderSystem::unregisterHandle(uint32_t entityIndex)
{
    handles_.erase(entityIndex);
}

void MeshRenderSystem::clear()
{
    handles_.clear();
    if (impl_) {
        impl_->textureResources_.clear();
        impl_->textureUploadBuffers_.clear();
        impl_->nextTextureSlot_ = 6;
    }
}

uint32_t MeshRenderSystem::uploadTexture(rendering::GpuDevice&    device,
                                          const tools::CpuTexture& tex)
{
    if (!impl_) return UINT32_MAX;
    auto& imp = *impl_;

    auto* dev = static_cast<ID3D12Device*>(device.nativeDevice());
    auto* cmd = static_cast<ID3D12GraphicsCommandList*>(device.nativeCommandList());

    const uint32_t mipCount = static_cast<uint32_t>(tex.mips.size());
    if (mipCount == 0) return UINT32_MAX;

    const uint32_t slot = imp.nextTextureSlot_++;

    // 1. Create DEFAULT-heap Texture2D (final GPU resource).
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width            = tex.baseWidth;
    texDesc.Height           = tex.baseHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels        = static_cast<UINT16>(mipCount);
    texDesc.Format           = static_cast<DXGI_FORMAT>(tex.dxgiFormat);
    texDesc.SampleDesc       = { 1, 0 };
    texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags            = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
    ENGINE_ASSERT(SUCCEEDED(dev->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&texResource))),
        "MeshRenderSystem::uploadTexture: failed to create texture resource");

    // 2. Get upload footprints for all mips.
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(mipCount);
    std::vector<UINT>   numRows(mipCount);
    std::vector<UINT64> rowSizes(mipCount);
    UINT64 totalBytes = 0;
    dev->GetCopyableFootprints(&texDesc, 0, mipCount, 0,
        layouts.data(), numRows.data(), rowSizes.data(), &totalBytes);

    // 3. Create UPLOAD-heap staging buffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuf;
    ENGINE_ASSERT(createUploadBuffer(dev, totalBytes, uploadBuf),
        "MeshRenderSystem::uploadTexture: failed to create upload buffer");

    // 4. Map and copy each mip into the upload buffer.
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    ENGINE_ASSERT(SUCCEEDED(uploadBuf->Map(0, &readRange,
        reinterpret_cast<void**>(&mapped))),
        "MeshRenderSystem::uploadTexture: failed to map upload buffer");

    for (uint32_t m = 0; m < mipCount; ++m) {
        const auto& mip      = tex.mips[m];
        const auto& fp       = layouts[m];
        const UINT  rowPitch = fp.Footprint.RowPitch;
        const UINT  srcPitch = mip.width * 4u; // RGBA8: 4 bytes/pixel
        uint8_t*   dst       = mapped + fp.Offset;
        const uint8_t* src   = mip.pixels.data();
        for (UINT row = 0; row < numRows[m]; ++row) {
            std::memcpy(dst + row * rowPitch, src + row * srcPitch,
                        srcPitch < rowPitch ? srcPitch : rowPitch);
        }
    }
    uploadBuf->Unmap(0, nullptr);

    // 5. Record copy commands for each mip.
    for (uint32_t m = 0; m < mipCount; ++m) {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource        = texResource.Get();
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = m;

        D3D12_TEXTURE_COPY_LOCATION src2 = {};
        src2.pResource       = uploadBuf.Get();
        src2.Type            = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src2.PlacedFootprint = layouts[m];

        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src2, nullptr);
    }

    // 6. Transition to SHADER_RESOURCE.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = texResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter  =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &barrier);

    // 7. Create SRV in the bindless heap at slot index.
    const UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const SIZE_T cpuBase = imp.srvHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                  = texDesc.Format;
    srvDesc.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels     = mipCount;
    dev->CreateShaderResourceView(texResource.Get(), &srvDesc,
        { cpuBase + static_cast<SIZE_T>(slot) * inc });

    // 8. Keep resources alive until clear() or destruction.
    imp.textureResources_.push_back(std::move(texResource));
    imp.textureUploadBuffers_.push_back(std::move(uploadBuf));

    return slot;
}

} // namespace engine::app
