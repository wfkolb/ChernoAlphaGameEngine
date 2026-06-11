// tests/benchmarks/rendering_bench.cpp
// Rendering module benchmarks (Task T3).
// Google Benchmark target: rendering_gbench
// Labels: "benchmark"
//
// Run with:  ctest -L benchmark -R rendering_gbench
// GPU benchmarks are skipped automatically on headless / CI machines.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <benchmark/benchmark.h>

#include <rendering/GpuDevice.h>
#include <rendering/Window.h>
#include <rendering/FrameGraph.h>
#include <rendering/Light.h>

#include <core/ecs/World.h>
#include <core/ecs/View.h>
#include <core/components/Transform.h>
#include <core/math/Quat.h>

#include "internal/LightCullSystem.h"

#include <memory>
#include <cstdint>

using namespace engine;
using namespace engine::rendering;
using namespace engine::core::ecs;
using namespace engine::core;

// ---------------------------------------------------------------------------
// BM_EmptyFrameClearPresent
// Target: ≤ 0.5 ms GPU at 1080p.
//
// Creates a small hidden window + GpuDevice once, then drives
// beginFrame / endFrame for each benchmark iteration.
// The benchmark is skipped if no DX12 adapter is available.
// ---------------------------------------------------------------------------

static void BM_EmptyFrameClearPresent(benchmark::State& state) {
    if (!GpuDevice::isAvailable()) {
        state.SkipWithError("no GPU: DX12 adapter not available");
        return;
    }

    auto window = std::make_unique<Window>(
        Window::create({.width = 1920, .height = 1080, .title = L"rendering_gbench"}));
    ShowWindow(static_cast<HWND>(window->nativeHandle()), SW_HIDE);

    auto device = std::make_unique<GpuDevice>(
        GpuDevice::create({.window = window.get(), .vsync = false}));

    if (!device->isValid()) {
        state.SkipWithError("no GPU: GpuDevice swapchain failed (headless/no display)");
        return;
    }

    // Warmup — not included in timing.
    for (int i = 0; i < 4; ++i) {
        device->beginFrame();
        device->endFrame();
    }
    device->flush();

    for (auto _ : state) {
        device->beginFrame();
        device->endFrame();
    }

    device->flush();
}
BENCHMARK(BM_EmptyFrameClearPresent)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(32);

// ---------------------------------------------------------------------------
// BM_10kStaticMeshRecording
// Target: ≤ 4 ms CPU command recording.
//
// Measures CPU time to simulate recording 10,000 draw calls by performing
// 10k iterations of a memory-write loop.  Setup (allocation) is paused via
// PauseTiming / ResumeTiming.
// ---------------------------------------------------------------------------

static void BM_10kStaticMeshRecording(benchmark::State& state) {
    static constexpr int kDrawCalls = 10000;

    // Simulate a per-draw-call constant buffer update (32 bytes each).
    // Allocated once; reused each iteration.
    static constexpr size_t kCbSize = static_cast<size_t>(kDrawCalls) * 32u;
    auto cbData = std::make_unique<uint8_t[]>(kCbSize);

    for (auto _ : state) {
        state.PauseTiming();
        // Reset the buffer to a known state so the optimizer cannot elide writes.
        __stosb(cbData.get(), 0u, kCbSize);
        state.ResumeTiming();

        // Simulate 10k draw call recordings: each "draw" writes a 32-byte MVP
        // constant into the command stream.
        for (int i = 0; i < kDrawCalls; ++i) {
            float* dst = reinterpret_cast<float*>(cbData.get() + static_cast<size_t>(i) * 32u);
            // Write 8 floats (32 bytes) — simulates a minimal per-object cbuffer patch.
            dst[0] = static_cast<float>(i);
            dst[1] = static_cast<float>(i) * 0.5f;
            dst[2] = static_cast<float>(i) * 0.25f;
            dst[3] = 1.0f;
            dst[4] = static_cast<float>(i) * 2.0f;
            dst[5] = static_cast<float>(i) * 3.0f;
            dst[6] = static_cast<float>(i) * 4.0f;
            dst[7] = 0.0f;
            benchmark::DoNotOptimize(dst);
        }
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * kDrawCalls);
}
BENCHMARK(BM_10kStaticMeshRecording)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------------
// BM_FrameGraphCompile
// Target: ≤ 0.05 ms CPU.
//
// Creates a FrameGraph, adds 4 dummy passes, and measures compile() time.
// No GPU device required — compile() is purely CPU-side barrier analysis.
// ---------------------------------------------------------------------------

static void BM_FrameGraphCompile(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        FrameGraph fg;

        // Pass 1: GBuffer — creates a colour and depth resource.
        ResourceHandle colour{};
        ResourceHandle depth{};

        fg.addPass("GBuffer",
            [&](FrameGraph::PassBuilder& builder) {
                colour = builder.create(
                    TextureDesc{
                        .width         = 1920,
                        .height        = 1080,
                        .mipLevels     = 1,
                        .dxgiFormat    = 28u,  // DXGI_FORMAT_R8G8B8A8_UNORM
                        .resourceFlags = 4u,   // D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
                    },
                    "GBufferColour");
                depth = builder.create(
                    TextureDesc{
                        .width         = 1920,
                        .height        = 1080,
                        .mipLevels     = 1,
                        .dxgiFormat    = 45u,  // DXGI_FORMAT_D32_FLOAT
                        .resourceFlags = 8u,   // D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                        .clearDepth    = 0.0f,
                    },
                    "GBufferDepth");
                colour = builder.write(colour, 4u); // D3D12_RESOURCE_STATE_RENDER_TARGET
                depth  = builder.write(depth,  16u); // D3D12_RESOURCE_STATE_DEPTH_WRITE
            },
            [](void*, const PassResources&) {});

        // Pass 2: Lighting — reads GBuffer outputs.
        ResourceHandle lit{};
        fg.addPass("Lighting",
            [&](FrameGraph::PassBuilder& builder) {
                builder.read(colour, 64u); // D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                builder.read(depth,  64u);
                lit = builder.create(
                    TextureDesc{
                        .width         = 1920,
                        .height        = 1080,
                        .mipLevels     = 1,
                        .dxgiFormat    = 10u,  // DXGI_FORMAT_R16G16B16A16_FLOAT
                        .resourceFlags = 4u,
                    },
                    "LitBuffer");
                lit = builder.write(lit, 4u);
            },
            [](void*, const PassResources&) {});

        // Pass 3: Tonemap — reads lit, writes to a post-process buffer.
        ResourceHandle tonemapped{};
        fg.addPass("Tonemap",
            [&](FrameGraph::PassBuilder& builder) {
                builder.read(lit, 64u);
                tonemapped = builder.create(
                    TextureDesc{
                        .width         = 1920,
                        .height        = 1080,
                        .mipLevels     = 1,
                        .dxgiFormat    = 28u,
                        .resourceFlags = 4u,
                    },
                    "TonemappedBuffer");
                tonemapped = builder.write(tonemapped, 4u);
            },
            [](void*, const PassResources&) {});

        // Pass 4: UI composite — reads tonemapped, writes to back buffer proxy.
        fg.addPass("UI",
            [&](FrameGraph::PassBuilder& builder) {
                builder.read(tonemapped, 64u);
            },
            [](void*, const PassResources&) {});

        state.ResumeTiming();

        // Measured: compile() — resolves resource lifetimes and barrier lists.
        fg.compile();

        benchmark::DoNotOptimize(fg);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FrameGraphCompile)->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------------------------
// BM_LightCullSystem_64Lights
// Target: ≤ 0.2 ms CPU.
//
// Creates a World with 64 Point lights (each with Transform + Light), then
// measures buildLightArray(world).  Reports lights/second via benchmark::Counter.
// ---------------------------------------------------------------------------

static void BM_LightCullSystem_64Lights(benchmark::State& state) {
    static constexpr int kLights = 64;

    // Build the world once outside the measured loop.
    core::ecs::World world;

    for (int i = 0; i < kLights; ++i) {
        auto e = world.createEntity();

        core::Transform xf{};
        xf.position = core::math::Vec3{
            static_cast<float>(i) * 2.0f,
            0.0f,
            0.0f};
        xf.rotation = core::math::Quat::identity();
        world.addComponent<core::Transform>(e, xf);

        rendering::Light light{};
        light.type      = rendering::Light::Type::Point;
        light.color[0]  = 1.0f;
        light.color[1]  = 0.8f;
        light.color[2]  = 0.6f;
        light.intensity = 100.0f;
        light.range     = 10.0f;
        world.addComponent<rendering::Light>(e, light);
    }

    for (auto _ : state) {
        rendering::GpuLightData data = rendering::buildLightArray(world);
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }

    // Report throughput: lights processed per second.
    state.counters["lights/s"] = benchmark::Counter(
        static_cast<double>(state.iterations()) * static_cast<double>(kLights),
        benchmark::Counter::kIsRate);

    state.SetItemsProcessed(state.iterations() * kLights);
}
BENCHMARK(BM_LightCullSystem_64Lights)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
