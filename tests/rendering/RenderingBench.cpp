// tests/rendering/RenderingBench.cpp
// GPU rendering benchmarks — run with: ctest -L gpu
//
// These tests are intentionally excluded from the "unit" label so that
// headless CI machines skip them.  On machines with DX12 hardware run:
//   ctest -L gpu

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gtest/gtest.h>

#include <rendering/GpuDevice.h>
#include <rendering/Window.h>

namespace engine::rendering {

// ---------------------------------------------------------------------------
// Fixture: creates a hidden 64×64 window + GpuDevice once per test.
// ---------------------------------------------------------------------------

class RenderingBenchFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!GpuDevice::isAvailable()) {
            GTEST_SKIP() << "No DX12-capable adapter — skipping GPU benchmark";
        }

        window_ = std::make_unique<Window>(
            Window::create({.width = 64, .height = 64, .title = L"RenderBench"}));
        ShowWindow(static_cast<HWND>(window_->nativeHandle()), SW_HIDE);

        device_ = std::make_unique<GpuDevice>(
            GpuDevice::create({.window = window_.get(), .vsync = false}));

        if (!device_->isValid()) {
            GTEST_SKIP() << "GpuDevice swapchain failed (headless/no display) — skipping GPU benchmark";
        }
    }

    void TearDown() override {
        if (device_ && device_->isValid()) {
            device_->flush();
        }
        device_.reset();
        window_.reset();
    }

    std::unique_ptr<Window>    window_;
    std::unique_ptr<GpuDevice> device_;
};

// ---------------------------------------------------------------------------
// BM_BeginEndFrame — measures the CPU cost of one beginFrame/endFrame cycle.
//
// The inner loop is deliberately small (kIterations = 32) to stay within
// a sensible wall-clock budget on any DX12 hardware.  The intent is to give
// a regression signal, not a precise GPU throughput number.
// ---------------------------------------------------------------------------

TEST_F(RenderingBenchFixture, BM_BeginEndFrame) {
    constexpr int kWarmupFrames    =  4;
    constexpr int kMeasuredFrames  = 32;

    // Warmup — not measured.
    for (int i = 0; i < kWarmupFrames; ++i) {
        device_->beginFrame();
        device_->endFrame();
    }
    device_->flush();

    // Timed run.
    const LARGE_INTEGER freq = []{
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f;
    }();

    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);

    for (int i = 0; i < kMeasuredFrames; ++i) {
        device_->beginFrame();
        device_->endFrame();
    }
    device_->flush();

    QueryPerformanceCounter(&t1);

    const double elapsedMs =
        static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 /
        static_cast<double>(freq.QuadPart);
    const double msPerFrame = elapsedMs / kMeasuredFrames;

    // Report — not a hard pass/fail threshold; just ensure it doesn't hang.
    SUCCEED() << "BM_BeginEndFrame: " << kMeasuredFrames << " frames in "
              << elapsedMs << " ms  (" << msPerFrame << " ms/frame)";

    // Sanity guard: if one frame takes more than 5 s something is very wrong.
    EXPECT_LT(msPerFrame, 5000.0)
        << "beginFrame/endFrame took > 5 s per frame — possible GPU hang";
}

} // namespace engine::rendering
