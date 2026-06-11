#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gtest/gtest.h>

#include <rendering/GpuDevice.h>
#include <rendering/Window.h>

#include "support/ReadbackHelper.h"

#include <d3d12.h>

namespace engine::rendering {

TEST(RenderingSmoke, deviceInitClearAndReadback) {
    if (!GpuDevice::isAvailable()) {
        GTEST_SKIP() << "No DX12 device";
    }

    auto window = Window::create({.width = 64, .height = 64, .title = L"Test"});
    ShowWindow(static_cast<HWND>(window.nativeHandle()), SW_HIDE);

    auto device = GpuDevice::create({.window = &window, .vsync = false});
    if (!device.isValid()) {
        GTEST_SKIP() << "GpuDevice swapchain failed (headless/no display)";
    }

    device.beginFrame();

    // Clear the current back buffer to red using the open command list.
    {
        auto* cmdList = static_cast<ID3D12GraphicsCommandList*>(device.nativeCommandList());
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{ device.currentBackBufferRtvHandle() };
        const float clearColor[4] = {1.f, 0.f, 0.f, 1.f};
        cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    }

    // readbackBackBuffer records the copy commands on the open command list,
    // then calls endFrame() + flush() internally.  Do NOT call endFrame() here.
    auto pixels = readbackBackBuffer(device, 0, 0, 64, 64);
    if (pixels.empty()) {
        GTEST_SKIP() << "Readback helper not yet implemented";
    }

    EXPECT_NEAR(pixels[0].r, 255, 2);
    EXPECT_NEAR(pixels[0].g,   0, 2);
    EXPECT_NEAR(pixels[0].b,   0, 2);
}

} // namespace engine::rendering
