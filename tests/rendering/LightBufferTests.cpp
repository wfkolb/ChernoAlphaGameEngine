// tests/rendering/LightBufferTests.cpp
// Unit tests for engine::rendering::buildLightArray (Task R1).
// No GPU / D3D12 required.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <gtest/gtest.h>

#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/math/Quat.h>
#include <rendering/Light.h>

#include "internal/LightCullSystem.h"

#include <cstring>
#include <cmath>

using namespace engine;

namespace {

// Mirror of the GpuLight layout (64 bytes) for test readback.
struct GpuLightReadback {
    float position[4];
    float direction[4];
    float color[4];
    float spotAngles[4];
};
static_assert(sizeof(GpuLightReadback) == 64);

GpuLightReadback readLight(const rendering::GpuLightData& data, uint32_t idx) {
    GpuLightReadback out{};
    std::memcpy(&out, data.bytes + idx * 64, 64);
    return out;
}

} // anonymous namespace

TEST(LightBufferTests, EmptyWorldGivesZeroCount) {
    core::ecs::World world;
    rendering::GpuLightData result = rendering::buildLightArray(world);
    EXPECT_EQ(result.count, 0u);
}

TEST(LightBufferTests, DirectionalLightPackedCorrectly) {
    core::ecs::World world;

    auto e = world.createEntity();
    core::Transform xf{};
    xf.rotation = core::math::Quat::identity(); // no rotation; forward = +Z
    world.addComponent<core::Transform>(e, xf);

    rendering::Light light{};
    light.type      = rendering::Light::Type::Directional;
    light.color[0]  = 1.0f;
    light.color[1]  = 0.5f;
    light.color[2]  = 0.25f;
    light.intensity = 2.0f;
    world.addComponent<rendering::Light>(e, light);

    rendering::GpuLightData result = rendering::buildLightArray(world);
    ASSERT_EQ(result.count, 1u);

    GpuLightReadback gpu = readLight(result, 0);

    // type = 0 (Directional) stored in position.w
    EXPECT_FLOAT_EQ(gpu.position[3], 0.0f);

    // color.rgb = light.color * intensity
    EXPECT_NEAR(gpu.color[0], 1.0f * 2.0f, 1e-5f);
    EXPECT_NEAR(gpu.color[1], 0.5f * 2.0f, 1e-5f);
    EXPECT_NEAR(gpu.color[2], 0.25f * 2.0f, 1e-5f);

    // direction.xyz = rotate(identity, {0,0,1}) = {0,0,1}
    EXPECT_NEAR(gpu.direction[0], 0.0f, 1e-5f);
    EXPECT_NEAR(gpu.direction[1], 0.0f, 1e-5f);
    EXPECT_NEAR(gpu.direction[2], 1.0f, 1e-5f);

    // direction.w = range stored as 1000 for directional
    EXPECT_FLOAT_EQ(gpu.direction[3], 1000.0f);
}

TEST(LightBufferTests, PointLightPackedCorrectly) {
    core::ecs::World world;

    auto e = world.createEntity();
    core::Transform xf{};
    xf.position = core::math::Vec3{3.0f, 4.0f, 5.0f};
    xf.rotation = core::math::Quat::identity();
    world.addComponent<core::Transform>(e, xf);

    rendering::Light light{};
    light.type      = rendering::Light::Type::Point;
    light.color[0]  = 0.8f;
    light.color[1]  = 0.6f;
    light.color[2]  = 0.4f;
    light.intensity = 100.0f;
    light.range     = 20.0f;
    world.addComponent<rendering::Light>(e, light);

    rendering::GpuLightData result = rendering::buildLightArray(world);
    ASSERT_EQ(result.count, 1u);

    GpuLightReadback gpu = readLight(result, 0);

    // type = 1 (Point) in position.w
    EXPECT_FLOAT_EQ(gpu.position[3], 1.0f);

    // position.xyz = transform.position
    EXPECT_NEAR(gpu.position[0], 3.0f, 1e-5f);
    EXPECT_NEAR(gpu.position[1], 4.0f, 1e-5f);
    EXPECT_NEAR(gpu.position[2], 5.0f, 1e-5f);

    // direction.w = range
    EXPECT_NEAR(gpu.direction[3], 20.0f, 1e-5f);

    // color.rgb = color * intensity
    EXPECT_NEAR(gpu.color[0], 0.8f * 100.0f, 1e-3f);
    EXPECT_NEAR(gpu.color[1], 0.6f * 100.0f, 1e-3f);
    EXPECT_NEAR(gpu.color[2], 0.4f * 100.0f, 1e-3f);
}

TEST(LightBufferTests, SpotLightAnglesPackedCorrectly) {
    core::ecs::World world;

    auto e = world.createEntity();
    core::Transform xf{};
    xf.rotation = core::math::Quat::identity();
    world.addComponent<core::Transform>(e, xf);

    rendering::Light light{};
    light.type           = rendering::Light::Type::Spot;
    light.intensity      = 1.0f;
    light.innerConeAngle = 0.2f; // radians
    light.outerConeAngle = 0.4f; // radians
    world.addComponent<rendering::Light>(e, light);

    rendering::GpuLightData result = rendering::buildLightArray(world);
    ASSERT_EQ(result.count, 1u);

    GpuLightReadback gpu = readLight(result, 0);

    // type = 2 (Spot) in position.w
    EXPECT_FLOAT_EQ(gpu.position[3], 2.0f);

    // spotAngles.x = cos(outerConeAngle)
    EXPECT_NEAR(gpu.spotAngles[0], std::cos(0.4f), 1e-5f);

    // spotAngles.y = 1 / (cos(inner) - cos(outer) + 0.0001)
    const float cosInner = std::cos(0.2f);
    const float cosOuter = std::cos(0.4f);
    const float expectedInvDiff = 1.0f / (cosInner - cosOuter + 0.0001f);
    EXPECT_NEAR(gpu.spotAngles[1], expectedInvDiff, 1e-4f);

    // spotAngles.z = -1 (no shadow)
    EXPECT_FLOAT_EQ(gpu.spotAngles[2], -1.0f);

    // color.w = cos(innerConeAngle)
    EXPECT_NEAR(gpu.color[3], std::cos(0.2f), 1e-5f);
}

TEST(LightBufferTests, MultipleLightsAllPacked) {
    core::ecs::World world;

    // Create 3 lights of different types.
    for (int i = 0; i < 3; ++i) {
        auto e = world.createEntity();
        core::Transform xf{};
        world.addComponent<core::Transform>(e, xf);

        rendering::Light light{};
        light.type = (i == 0) ? rendering::Light::Type::Directional
                   : (i == 1) ? rendering::Light::Type::Point
                               : rendering::Light::Type::Spot;
        world.addComponent<rendering::Light>(e, light);
    }

    rendering::GpuLightData result = rendering::buildLightArray(world);
    EXPECT_EQ(result.count, 3u);
}

TEST(LightBufferTests, ExcessLightsCappedAt64) {
    core::ecs::World world;

    for (uint32_t i = 0; i < 80; ++i) {
        auto e = world.createEntity();
        core::Transform xf{};
        world.addComponent<core::Transform>(e, xf);

        rendering::Light light{};
        world.addComponent<rendering::Light>(e, light);
    }

    rendering::GpuLightData result = rendering::buildLightArray(world);
    EXPECT_EQ(result.count, 64u);
}

TEST(LightBufferTests, EntityWithoutLightNotIncluded) {
    core::ecs::World world;

    // Entity with Transform only — no Light
    auto noLight = world.createEntity();
    core::Transform xf{};
    world.addComponent<core::Transform>(noLight, xf);

    // Entity with Light
    auto withLight = world.createEntity();
    world.addComponent<core::Transform>(withLight, xf);
    rendering::Light light{};
    world.addComponent<rendering::Light>(withLight, light);

    rendering::GpuLightData result = rendering::buildLightArray(world);
    EXPECT_EQ(result.count, 1u);
}
