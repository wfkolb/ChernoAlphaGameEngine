#pragma once

#include <core/diag/Assert.h>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

namespace engine::rendering {

    // -------------------------------------------------------------------------
    // ResourceHandle
    // Opaque index into the frame graph's resource table. Invalid == 0xFFFF.
    // -------------------------------------------------------------------------
    struct ResourceHandle {
        uint16_t id { 0xFFFFu };

        constexpr bool isValid() const noexcept { return id != 0xFFFFu; }

        constexpr bool operator==(const ResourceHandle& r) const noexcept { return id == r.id; }
        constexpr bool operator!=(const ResourceHandle& r) const noexcept { return id != r.id; }
    };

    inline constexpr ResourceHandle kInvalidResource{};

    // -------------------------------------------------------------------------
    // TextureDesc
    // Describes a transient or persistent texture resource.
    // dxgiFormat and resourceFlags are stored as raw uint32_t to keep this
    // header free of DX12 types. Cast to DXGI_FORMAT / D3D12_RESOURCE_FLAGS
    // inside rendering internals.
    // clearDepth: use 0.0f for reverse-Z (far plane).
    // -------------------------------------------------------------------------
    struct TextureDesc {
        uint32_t width         { 0 };
        uint32_t height        { 0 };
        uint32_t mipLevels     { 1 };
        uint32_t dxgiFormat    { 0 };      // DXGI_FORMAT as raw uint32_t
        uint32_t resourceFlags { 0 };      // D3D12_RESOURCE_FLAGS as raw uint32_t
        float    clearColor[4] { 0.0f, 0.0f, 0.0f, 1.0f };
        float    clearDepth    { 0.0f };   // 0.0 = far (reverse-Z convention)
        uint8_t  clearStencil  { 0 };
    };

    // -------------------------------------------------------------------------
    // PassResources
    // Resolved resource accessors available inside an execute lambda.
    // All handles must have been declared via PassBuilder in the setup lambda.
    // -------------------------------------------------------------------------
    class FrameGraph;

    class PassResources {
    public:
        // Returns the underlying ID3D12Resource* as void*. Cast only inside rendering internals.
        void*    getResource(ResourceHandle h) const;

        // Returns D3D12_CPU_DESCRIPTOR_HANDLE.ptr for RTVs / DSVs (CPU-visible).
        uint64_t getRtvHandle(ResourceHandle h) const;
        uint64_t getDsvHandle(ResourceHandle h) const;

        // Returns D3D12_GPU_DESCRIPTOR_HANDLE.ptr for SRVs / UAVs (shader-visible heap).
        uint64_t getSrvHandle(ResourceHandle h) const;
        uint64_t getUavHandle(ResourceHandle h) const;

    private:
        friend class FrameGraph;
        struct Impl;
        const Impl* impl_ { nullptr };
    };

    // -------------------------------------------------------------------------
    // FrameGraph
    // Declarative render-pass graph. Build passes with addPass(), then compile()
    // once, then execute() each frame. Call reset() at the top of each frame
    // before adding passes.
    //
    // All void* command list parameters accept ID3D12GraphicsCommandList*.
    // Cast inside rendering internals only.
    // -------------------------------------------------------------------------

    // Signature of a pass execute callback.
    // First arg: ID3D12GraphicsCommandList* as void*.
    using ExecuteFn = std::function<void(void* cmdList, const PassResources&)>;

    class FrameGraph {
    public:
        // -----------------------------------------------------------------
        // PassBuilder — available only during the setup lambda of addPass().
        // -----------------------------------------------------------------
        class PassBuilder {
        public:
            // Declare a read dependency. requiredState is D3D12_RESOURCE_STATES as uint32_t.
            ResourceHandle read  (ResourceHandle h, uint32_t requiredState);

            // Declare a write dependency (produces a new resource version).
            ResourceHandle write (ResourceHandle h, uint32_t requiredState);

            // Allocate a transient texture resource for this frame only.
            ResourceHandle create(const TextureDesc& desc, std::string_view name);

            // Import an externally-owned resource into the graph.
            // resource    : ID3D12Resource* as void*.
            // currentState: D3D12_RESOURCE_STATES as uint32_t.
            ResourceHandle import(void*        resource,
                                  uint32_t     currentState,
                                  std::string_view name);
        private:
            friend class FrameGraph;
            struct Impl;
            Impl* impl_ { nullptr };

            explicit PassBuilder(Impl* impl) noexcept;
        };

        // -----------------------------------------------------------------
        // Public interface
        // -----------------------------------------------------------------

        // Register a render pass.
        // setup   — called immediately; use PassBuilder to declare read/write/create/import.
        // execute — called during execute(); receives the open command list and resolved resources.
        void addPass(std::string_view name,
                     std::function<void(PassBuilder&)> setup,
                     ExecuteFn                         execute);

        // Import the back buffer before building passes.
        // backBuf     : ID3D12Resource* as void*.
        // rtvHandle   : D3D12_CPU_DESCRIPTOR_HANDLE.ptr.
        // currentState: D3D12_RESOURCE_STATES as uint32_t (typically PRESENT).
        ResourceHandle importBackBuffer (void* backBuf,
                                         uint64_t rtvHandle,
                                         uint32_t currentState);

        // Import the depth buffer before building passes.
        // depth    : ID3D12Resource* as void*.
        // dsvHandle: D3D12_CPU_DESCRIPTOR_HANDLE.ptr.
        ResourceHandle importDepthBuffer(void* depth,
                                         uint64_t dsvHandle);

        // Resolve resource states and prepare barrier lists. Call once per frame
        // after all passes are added.
        void compile();

        // Record all pass execute lambdas (with auto-inserted barriers) into cmdList.
        // cmdList: ID3D12GraphicsCommandList* as void*.
        void execute(void* cmdList);

        // Reset the pass list and resource table. Call at the top of each frame.
        void reset();

        ENGINE_NO_COPY(FrameGraph);

        FrameGraph();
        FrameGraph(FrameGraph&&) noexcept;
        FrameGraph& operator=(FrameGraph&&) noexcept;
        ~FrameGraph();

        // Impl is forward-declared public so internal helpers (e.g. frameGraphSetDevice)
        // can name it. The definition lives in internal/FrameGraphImpl.h.
        struct Impl;

    private:
        std::unique_ptr<Impl> impl_;
        friend void frameGraphSetDevice(FrameGraph&, void*);
    };

    // -------------------------------------------------------------------------
    // Free helper — sets a fullscreen viewport and matching scissor rect on
    // cmdList (ID3D12GraphicsCommandList* as void*).
    // -------------------------------------------------------------------------
    void setFullscreenViewportScissor(void* cmdList, uint32_t width, uint32_t height);

} // namespace engine::rendering
