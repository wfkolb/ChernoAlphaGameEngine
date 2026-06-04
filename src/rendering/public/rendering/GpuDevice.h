#pragma once

#include <rendering/Window.h>
#include <core/diag/Assert.h>
#include <cstdint>
#include <memory>

namespace engine::rendering {

    class GpuDevice {
    public:
        struct Desc {
            Window* window { nullptr };
            bool    vsync  { true };
        };

        static GpuDevice create(const Desc& desc);

        // Probes DX12 support without creating a full device. Safe to call before create().
        static bool isAvailable();

        // Returns false when create() could not initialise the swapchain (e.g. headless/CI).
        // beginFrame / endFrame / flush are no-ops on an invalid device.
        bool isValid() const noexcept;

        uint32_t clientWidth()       const noexcept;
        uint32_t clientHeight()      const noexcept;
        bool     tearingSupported()  const noexcept;

        // Returns the highest supported D3D_FEATURE_LEVEL cast to uint32_t.
        // e.g. 0xC100 == D3D_FEATURE_LEVEL_12_1
        uint32_t featureLevel()      const noexcept;

        bool     dxrSupported()      const noexcept;

        // Wait for the oldest in-flight frame, reset the command allocator, and open the
        // command list for recording.
        void beginFrame();

        // Close the command list, execute it on the graphics queue, signal the fence, and present.
        void endFrame();

        // Signal a high fence value and block until the GPU drains completely.
        // Must be called before resize and before destruction of any GPU resources.
        void flush();

        // Escape hatches — for FrameGraph and PIX integration only.
        // Returns ID3D12Device* as void*. Do not dereference outside rendering internals.
        void* nativeDevice()          const noexcept;
        // Returns ID3D12GraphicsCommandList* for the current frame as void*.
        void* nativeCommandList()     const noexcept;
        // Returns ID3D12CommandQueue* (direct queue) as void*. For ImGui DX12 backend init only.
        void* nativeCommandQueue()    const noexcept;

        // Returns the D3D12_CPU_DESCRIPTOR_HANDLE.ptr for the current back buffer's RTV.
        uint64_t currentBackBufferRtvHandle() const noexcept;
        // Returns the D3D12_CPU_DESCRIPTOR_HANDLE.ptr for the depth buffer's DSV.
        uint64_t depthBufferDsvHandle()       const noexcept;

        // 0-based index of the back buffer currently being rendered into.
        uint32_t currentFrameIndex()  const noexcept;

        // Returns the current back buffer ID3D12Resource* as void*. For readback helpers only.
        void* nativeBackBuffer()  const noexcept;

        // Returns the depth buffer ID3D12Resource* as void*. For FrameGraph import only.
        void* nativeDepthBuffer() const noexcept;

        static constexpr uint32_t kBackBufferCount   = 3;
        static constexpr uint32_t kMaxFramesInFlight = 2;

        ENGINE_NO_COPY(GpuDevice);

        GpuDevice(GpuDevice&&) noexcept;
        GpuDevice& operator=(GpuDevice&&) noexcept;
        ~GpuDevice();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        explicit GpuDevice(std::unique_ptr<Impl> impl) noexcept;
    };

} // namespace engine::rendering
