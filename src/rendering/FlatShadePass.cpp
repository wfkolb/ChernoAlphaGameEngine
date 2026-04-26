#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "rendering/FlatShadePass.h"
#include "rendering/MeshManager.h"
#include "internal/FlatShadePipeline.h"

#include <d3d12.h>
#include <array>

namespace engine::rendering {

// D3D12_RESOURCE_STATE_RENDER_TARGET = 4
static constexpr uint32_t kStateRenderTarget = 4u;
// D3D12_RESOURCE_STATE_DEPTH_WRITE   = 0x20
static constexpr uint32_t kStateDepthWrite   = 0x20u;

void addFlatShadePass(
    FrameGraph&              fg,
    const MeshManager&       mm,
    MeshHandle               mesh,
    const FlatShadePipeline& pipeline,
    const float              mvp[16],
    ResourceHandle           backBuffer,
    ResourceHandle           depthBuffer,
    uint32_t                 width,
    uint32_t                 height)
{
    // Copy the MVP so the execute lambda owns the data independently of the caller's stack.
    std::array<float, 16> mvpCopy;
    for (int i = 0; i < 16; ++i) {
        mvpCopy[i] = mvp[i];
    }

    fg.addPass(
        "FlatShadePass",

        // ----------------------------------------------------------------
        // Setup lambda — declare resource dependencies
        // ----------------------------------------------------------------
        [backBuffer, depthBuffer](FrameGraph::PassBuilder& b) {
            b.write(backBuffer,  kStateRenderTarget);
            b.read (depthBuffer, kStateDepthWrite);
        },

        // ----------------------------------------------------------------
        // Execute lambda — record draw commands
        // ----------------------------------------------------------------
        [&mm, mesh, &pipeline, mvpCopy, backBuffer, depthBuffer, width, height]
        (void* cmdListVoid, const PassResources& res)
        {
            auto* cmd = static_cast<ID3D12GraphicsCommandList*>(cmdListVoid);

            // Bind render target and depth buffer.
            D3D12_CPU_DESCRIPTOR_HANDLE rtv;
            rtv.ptr = res.getRtvHandle(backBuffer);
            D3D12_CPU_DESCRIPTOR_HANDLE dsv;
            dsv.ptr = res.getDsvHandle(depthBuffer);

            cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

            static constexpr float kClearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
            cmd->ClearRenderTargetView(rtv, kClearColor, 0, nullptr);
            cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

            // Set viewport and scissor rect.
            setFullscreenViewportScissor(cmdListVoid, width, height);

            // Bind pipeline state, vertex / index buffers, and MVP root constants.
            bindFlatShade(
                pipeline,
                cmd,
                mm.vertexBufferView(mesh),
                mm.indexBufferView(mesh),
                mvpCopy.data());

            // Draw.
            cmd->DrawIndexedInstanced(mm.indexCount(mesh), 1, 0, 0, 0);
        }
    );
}

} // namespace engine::rendering
