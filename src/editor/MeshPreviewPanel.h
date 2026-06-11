#pragma once
#ifdef ENGINE_DEVREL

#include <rendering/GpuDevice.h>
#include <rendering/FrameGraph.h>
#include <rendering/MeshManager.h>
#include <rendering/Mesh.h>

#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
#include <memory>

#include <imgui.h>

namespace engine::rendering { struct OpaquePassPipeline; }

namespace engine::editor {

// Renders a single .easset mesh into a 256×256 offscreen render target and
// displays it in an ImGui window with an interactive orbit camera.
//
// Integration in EditorApp:
//   1. init() — once, after GpuDevice is ready and an SRV slot is allocated.
//   2. setMeshManager() — each frame after the lazy MeshManager is created.
//   3. loadAsset(path) — when the user selects an .easset in the browser.
//   4. render(cmdList) — each frame before ImGui::NewFrame.
//   5. postFrameBarrier(cmdList) — after ImGui render / endFrame.
//   6. draw() — inside the ImGui panel loop.
class MeshPreviewPanel {
public:
    MeshPreviewPanel();
    ~MeshPreviewPanel();

    void init(rendering::GpuDevice& device,
              uint64_t srvCpuHandle,
              uint64_t srvGpuHandle);

    void setMeshManager(rendering::MeshManager* mm) noexcept { meshManager_ = mm; }

    // Load or reload an .easset. No-op when path is unchanged.
    // Requires meshManager to be non-null.
    void loadAsset(const std::filesystem::path& path);

    // Record mesh draw into the offscreen RT, then transition RT→PSR.
    void render(void* cmdList);

    // Transition RT PSR→RENDER_TARGET (call after ImGui / endFrame).
    void postFrameBarrier(void* cmdList);

    // Draw the "Asset Preview" ImGui window.
    void draw(bool* open = nullptr);

    // Draw the preview image inline (no ImGui::Begin/End).
    // Call inside an already-open child window or panel.
    void drawInline(ImVec2 size);

    bool hasMesh() const noexcept { return hasMesh_; }

private:
    void buildRT();
    void updateSrv();

    static constexpr uint32_t kW = 256;
    static constexpr uint32_t kH = 256;

    rendering::GpuDevice*   device_      = nullptr;
    rendering::MeshManager* meshManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource>       colorRt_;
    Microsoft::WRL::ComPtr<ID3D12Resource>       depthRt_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
    D3D12_CPU_DESCRIPTOR_HANDLE                  rtvCpu_{};
    D3D12_CPU_DESCRIPTOR_HANDLE                  dsvCpu_{};

    uint64_t srvCpuHandle_ = 0;
    uint64_t srvGpuHandle_ = 0;

    std::unique_ptr<rendering::OpaquePassPipeline> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         perFrameBuf_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         perObjectBuf_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         materialsBuf_;
    Microsoft::WRL::ComPtr<ID3D12Resource>         lightsBuf_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>   nullSrvHeap_;
    void* perFramePtr_  = nullptr;
    void* perObjectPtr_ = nullptr;
    UINT  srvDescSize_  = 0;
    rendering::FrameGraph                          fg_;

    rendering::MeshHandle meshHandle_{};
    std::filesystem::path loadedPath_;
    bool                  hasMesh_      = false;
    bool                  initialized_  = false;
    bool                  rtInSrvState_ = false;

    // Orbit camera
    float yaw_           = 0.7854f; // 45 deg
    float pitch_         = 0.4363f; // 25 deg
    float radius_        = 3.0f;
    float defaultRadius_ = 3.0f;    // reset target for Recenter
};

} // namespace engine::editor

#endif // ENGINE_DEVREL
