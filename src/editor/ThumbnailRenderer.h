#pragma once
#ifdef ENGINE_DEVREL

#include <rendering/FrameGraph.h>
#include <rendering/MeshManager.h>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

#include <wrl/client.h>
#include <imgui.h>

struct ID3D12Resource;
struct ID3D12DescriptorHeap;

namespace engine::rendering { class GpuDevice; struct OpaquePassPipeline; }

namespace engine::editor {

class ThumbnailRenderer {
public:
    ThumbnailRenderer();
    ~ThumbnailRenderer();

    ThumbnailRenderer(const ThumbnailRenderer&) = delete;
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete;

    // SRV alloc callback: fills outCpuPtr + outGpuPtr from the shared ImGui heap.
    using SrvAllocFn = std::function<void(uint64_t& outCpuPtr, uint64_t& outGpuPtr)>;

    // Must be called once, before flushPending is ever called.
    // srvAlloc is captured and used to allocate SRV slots for rendered thumbnails.
    void init(rendering::GpuDevice& device, SrvAllocFn srvAlloc);

    // Set the shared MeshManager (available after the first beginFrame in EditorApp).
    void setMeshManager(rendering::MeshManager* mm) noexcept { meshManager_ = mm; }

    // Queue a thumbnail render for path (no-op if already cached).
    void requestThumbnail(const std::filesystem::path& path);

    // Process pending thumbnail requests using the current frame's open command list.
    // Call each frame AFTER beginFrame(), BEFORE ImGui::NewFrame().
    // cmdList is ID3D12GraphicsCommandList*.
    void flushPending(void* cmdList);

    // Returns GPU SRV handle for ImGui::Image, or nullptr if not yet rendered.
    ImTextureID getImGuiTexture(const std::filesystem::path& path) const;

    bool isInitialized() const noexcept { return initialized_; }

private:
    struct ThumbnailEntry {
        uint64_t srvGpuHandle = 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> colorRt;
        Microsoft::WRL::ComPtr<ID3D12Resource> depthRt;
        bool rendered = false;
    };

    bool                                     initialized_  = false;
    rendering::GpuDevice*                    device_       = nullptr;
    rendering::MeshManager*                  meshManager_  = nullptr;
    SrvAllocFn                               srvAlloc_;
    std::unique_ptr<rendering::OpaquePassPipeline> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         perFrameBuf_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         perObjectBuf_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         materialsBuf_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         lightsBuf_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>   nullSrvHeap_;
    void* perFramePtr_  = nullptr;
    void* perObjectPtr_ = nullptr;
    UINT  srvDescSize_  = 0;
    rendering::FrameGraph                    thumbFg_;

    // Non-shader-visible heaps for thumbnail RTV + DSV.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    uint32_t nextRtvSlot_ = 0;
    uint32_t nextDsvSlot_ = 0;

    std::unordered_map<std::string, ThumbnailEntry> cache_;
    std::vector<std::filesystem::path>               pending_;
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
