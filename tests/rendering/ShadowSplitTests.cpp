// tests/rendering/ShadowSplitTests.cpp
// Unit tests for engine::rendering::computeCascadeSplits (Task C1).
// CPU-only — no GPU / D3D12 required.

#include <gtest/gtest.h>

#include <ShadowPass.h>

#include <cmath>
#include <array>

using namespace engine::rendering;

namespace {

// Helper: compute the fully-uniform split distances analytically.
// splitUnif[i] = near + (far - near) * (i+1) / n
float uniformSplit(float nearZ, float farZ, int n, int i) {
    return nearZ + (farZ - nearZ) * static_cast<float>(i + 1) / static_cast<float>(n);
}

// Helper: compute the purely-logarithmic split distances analytically.
// splitLog[i] = near * (far/near)^((i+1)/n)
float logSplit(float nearZ, float farZ, int n, int i) {
    return nearZ * std::pow(farZ / nearZ, static_cast<float>(i + 1) / static_cast<float>(n));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// lambda = 0.0 → pure uniform splits
// ---------------------------------------------------------------------------
TEST(ShadowSplitTests, LambdaZeroGivesUniformSplits) {
    constexpr float nearZ = 0.1f;
    constexpr float farZ  = 1000.0f;
    constexpr int   n     = static_cast<int>(kShadowCascadeCount);

    auto splits = computeCascadeSplits(nearZ, farZ, n, 0.0f);

    for (int i = 0; i < n; ++i) {
        const float expected = uniformSplit(nearZ, farZ, n, i);
        EXPECT_NEAR(splits[static_cast<size_t>(i)], expected, 1e-3f)
            << "cascade " << i;
    }
}

// ---------------------------------------------------------------------------
// lambda = 1.0 → pure logarithmic splits
// ---------------------------------------------------------------------------
TEST(ShadowSplitTests, LambdaOneGivesLogarithmicSplits) {
    constexpr float nearZ = 0.1f;
    constexpr float farZ  = 1000.0f;
    constexpr int   n     = static_cast<int>(kShadowCascadeCount);

    auto splits = computeCascadeSplits(nearZ, farZ, n, 1.0f);

    for (int i = 0; i < n; ++i) {
        const float expected = logSplit(nearZ, farZ, n, i);
        EXPECT_NEAR(splits[static_cast<size_t>(i)], expected, 1e-3f)
            << "cascade " << i;
    }
}

// ---------------------------------------------------------------------------
// lambda = 0.95 → monotonically increasing, values between uniform and log
// ---------------------------------------------------------------------------
TEST(ShadowSplitTests, LambdaDefaultMonotonicallyIncreasing) {
    constexpr float nearZ  = 0.1f;
    constexpr float farZ   = 1000.0f;
    constexpr int   n      = static_cast<int>(kShadowCascadeCount);
    constexpr float lambda = 0.95f;

    auto splits = computeCascadeSplits(nearZ, farZ, n, lambda);

    // Monotonically increasing.
    for (int i = 1; i < n; ++i) {
        EXPECT_GT(splits[static_cast<size_t>(i)], splits[static_cast<size_t>(i - 1)])
            << "splits not monotone at index " << i;
    }

    // All values should lie between the pure-uniform and pure-log extremes
    // (for near < far and each individual cascade).
    for (int i = 0; i < n; ++i) {
        const float lo = std::min(uniformSplit(nearZ, farZ, n, i),
                                  logSplit   (nearZ, farZ, n, i));
        const float hi = std::max(uniformSplit(nearZ, farZ, n, i),
                                  logSplit   (nearZ, farZ, n, i));
        EXPECT_GE(splits[static_cast<size_t>(i)], lo - 1e-3f)
            << "split[" << i << "] below expected minimum";
        EXPECT_LE(splits[static_cast<size_t>(i)], hi + 1e-3f)
            << "split[" << i << "] above expected maximum";
    }
}

// ---------------------------------------------------------------------------
// Last cascade should equal farZ (both uniform and log converge at the end)
// ---------------------------------------------------------------------------
TEST(ShadowSplitTests, LastCascadeEqualsfarZ) {
    constexpr float nearZ = 0.5f;
    constexpr float farZ  = 500.0f;
    constexpr int   n     = static_cast<int>(kShadowCascadeCount);

    for (float lambda : { 0.0f, 0.5f, 1.0f }) {
        auto splits = computeCascadeSplits(nearZ, farZ, n, lambda);
        EXPECT_NEAR(splits[static_cast<size_t>(n - 1)], farZ, 1e-2f)
            << "last cascade should equal farZ for lambda=" << lambda;
    }
}

// ---------------------------------------------------------------------------
// Lambda clamping: values outside [0,1] are clamped
// ---------------------------------------------------------------------------
TEST(ShadowSplitTests, LambdaClampedToZeroOne) {
    constexpr float nearZ = 0.1f;
    constexpr float farZ  = 1000.0f;
    constexpr int   n     = static_cast<int>(kShadowCascadeCount);

    auto splitsNeg  = computeCascadeSplits(nearZ, farZ, n, -0.5f);
    auto splitsZero = computeCascadeSplits(nearZ, farZ, n,  0.0f);
    auto splitsOver = computeCascadeSplits(nearZ, farZ, n,  2.0f);
    auto splitsOne  = computeCascadeSplits(nearZ, farZ, n,  1.0f);

    for (int i = 0; i < n; ++i) {
        EXPECT_FLOAT_EQ(splitsNeg[static_cast<size_t>(i)],
                        splitsZero[static_cast<size_t>(i)])
            << "negative lambda should behave like 0 at index " << i;
        EXPECT_FLOAT_EQ(splitsOver[static_cast<size_t>(i)],
                        splitsOne[static_cast<size_t>(i)])
            << "lambda>1 should behave like 1 at index " << i;
    }
}
