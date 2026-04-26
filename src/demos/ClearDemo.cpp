#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d12.h>
#include <rendering/Window.h>
#include <rendering/GpuDevice.h>
#include <cstdlib>

// Minimal visual test — no FrameGraph, no mesh, no shaders.
// Clears the back buffer to bright green every frame.
// If you see green: device/swapchain/present pipeline is alive.
// If still black: the problem is at the device or window level.

int main()
{
    using namespace engine::rendering;

    Window window = Window::create({ .width = 1280, .height = 720, .title = L"ClearDemo" });
    GpuDevice device = GpuDevice::create({ .window = &window, .vsync = true });
    if (!device.isValid()) return EXIT_FAILURE;

    while (!window.wantsClose())
    {
        device.beginFrame();

        auto* cmd = static_cast<ID3D12GraphicsCommandList*>(device.nativeCommandList());

        D3D12_CPU_DESCRIPTOR_HANDLE rtv{ device.currentBackBufferRtvHandle() };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{ device.depthBufferDsvHandle() };

        cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

        constexpr float kGreen[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
        cmd->ClearRenderTargetView(rtv, kGreen, 0, nullptr);
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);

        device.endFrame();
    }

    device.flush();
    return EXIT_SUCCESS;
}
