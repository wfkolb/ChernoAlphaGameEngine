// tests/benchmarks/core_bench.cpp
// Core module microbenchmarks (Task #38).

#include <benchmark/benchmark.h>
#include "core/math/Frustum.h"
#include "core/math/Mat.h"
#include "core/math/Quat.h"
#include "core/ecs/World.h"
#include "core/ecs/View.h"

#include <memory>

using namespace engine::core::math;
using namespace engine::core::ecs;

// ---------------------------------------------------------------------------
// Math benchmarks
// ---------------------------------------------------------------------------

static void BM_Mat4Multiply(benchmark::State& state) {
    Mat4 a = rotationY(1.23f);
    Mat4 b = rotationX(0.77f);
    for (auto _ : state) {
        benchmark::DoNotOptimize(a * b);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_Mat4Multiply)->Iterations(10000000);

static void BM_QuatSlerp(benchmark::State& state) {
    Quat a = fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.5f);
    Quat b = fromAxisAngle({1.0f, 0.0f, 0.0f}, 1.2f);
    for (auto _ : state) {
        benchmark::DoNotOptimize(slerp(a, b, 0.5f));
        benchmark::ClobberMemory();
    }
}
BENCHMARK(BM_QuatSlerp)->Iterations(10000000);

static void BM_FrustumCull_10k(benchmark::State& state) {
    // Build a view-projection matrix for a perspective camera looking down +Z.
    // FOV 60 degrees, 16:9 aspect, near=0.1, far=1000.0.
    const Mat4 view = lookAtRh(
        Vec3{0.0f, 0.0f, -10.0f},   // eye
        Vec3{0.0f, 0.0f,   0.0f},   // target
        Vec3{0.0f, 1.0f,   0.0f});  // up
    const Mat4 proj = perspectiveRhYupReverseZ(
        1.04719755f,   // ~60 degrees in radians
        16.0f / 9.0f,
        0.1f,
        1000.0f);
    const Mat4 vp = view * proj;
    const Frustum frustum = frustumFromViewProj(vp);

    // Pre-generate 10,000 sphere queries spread across a wide volume so that
    // roughly half pass the cull test and half are rejected.
    static constexpr int kCount = 10000;
    struct Sphere { Vec3 center; float radius; };
    Sphere spheres[kCount];
    {
        // Simple deterministic LCG to avoid pulling in <random>.
        unsigned seed = 0x12345678u;
        auto lcg = [&seed]() -> float {
            seed = seed * 1664525u + 1013904223u;
            // Map [0, UINT_MAX] to [-1, 1]
            return static_cast<float>(seed) / static_cast<float>(0xFFFFFFFFu) * 2.0f - 1.0f;
        };
        for (int i = 0; i < kCount; ++i) {
            spheres[i].center = Vec3{lcg() * 500.0f, lcg() * 500.0f, lcg() * 500.0f};
            spheres[i].radius = 1.0f + (lcg() * 0.5f + 0.5f) * 9.0f;  // [1, 10]
        }
    }

    int visible = 0;
    for (auto _ : state) {
        visible = 0;
        for (int i = 0; i < kCount; ++i) {
            bool inside = frustumContainsSphere(frustum, spheres[i].center, spheres[i].radius);
            benchmark::DoNotOptimize(inside);
            visible += static_cast<int>(inside);
        }
        benchmark::DoNotOptimize(visible);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kCount);
}
BENCHMARK(BM_FrustumCull_10k);

// ---------------------------------------------------------------------------
// ECS benchmark components
// ---------------------------------------------------------------------------

struct BenchTransform {
    float m[16];
    static constexpr ComponentTypeId kComponentId = 0;
};
struct BenchVelocity {
    float v[3];
    static constexpr ComponentTypeId kComponentId = 1;
};
struct BenchTag {
    int t;
    static constexpr ComponentTypeId kComponentId = 2;
};

// ---------------------------------------------------------------------------
// ECS benchmark fixture
// ---------------------------------------------------------------------------

class EcsBenchFixture : public benchmark::Fixture {
public:
    void SetUp(const benchmark::State&) override {
        if (world_) return;
        world_ = std::make_unique<World>();

        // Archetype A: Transform + Velocity (5000 entities)
        for (int i = 0; i < 5000; ++i) {
            auto e = world_->createEntity();
            world_->addComponent<BenchTransform>(e);
            world_->addComponent<BenchVelocity>(e);
        }
        // Archetype B: Transform + Velocity + Tag (3000 entities)
        for (int i = 0; i < 3000; ++i) {
            auto e = world_->createEntity();
            world_->addComponent<BenchTransform>(e);
            world_->addComponent<BenchVelocity>(e);
            world_->addComponent<BenchTag>(e);
        }
        // Archetype C: Transform only (2000 entities) — excluded from View<Transform,Velocity>
        for (int i = 0; i < 2000; ++i) {
            auto e = world_->createEntity();
            world_->addComponent<BenchTransform>(e);
        }
    }

    void TearDown(const benchmark::State&) override {}

    std::unique_ptr<World> world_;
};

BENCHMARK_DEFINE_F(EcsBenchFixture, BM_EcsViewIterate_10k)(benchmark::State& state) {
    for (auto _ : state) {
        View<BenchTransform, BenchVelocity> view(*world_);
        for (auto [e, t, v] : view) {
            benchmark::DoNotOptimize(t);
            benchmark::DoNotOptimize(v);
        }
        benchmark::ClobberMemory();
    }
}
BENCHMARK_REGISTER_F(EcsBenchFixture, BM_EcsViewIterate_10k);
