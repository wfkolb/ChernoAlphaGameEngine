#include "internal/LightCullSystem.h"

#include <rendering/Light.h>
#include <core/components/Transform.h>
#include <core/ecs/World.h>
#include <core/ecs/View.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/math/Quat.h>

#include <cmath>
#include <cstring>

namespace engine::rendering {

namespace {

// C++ mirror of the HLSL GpuLight struct (64 bytes, no padding).
struct GpuLight {
    float position[4];   // xyz=world pos or unused for directional, w=type
    float direction[4];  // xyz=normalized direction (world space), w=range
    float color[4];      // rgb=color*intensity (linear), w=innerCosAngle
    float spotAngles[4]; // x=cos(outerCone), y=1/(cosInner-cosOuter), z=shadowIdx, w=unused
};
static_assert(sizeof(GpuLight) == 64, "GpuLight must be 64 bytes");

} // anonymous namespace

GpuLightData buildLightArray(core::ecs::World& world) noexcept {
    GpuLightData result{};
    result.count = 0;

    core::ecs::View<core::Transform, Light> view(world);
    for (auto [entity, transform, light] : view) {
        if (result.count >= GpuLightData::kMaxLights) break;

        // Resolve world transform — walk hierarchy if this light has a parent.
        core::Transform worldTr = transform;
        const auto* hc = world.tryGet<core::ecs::HierarchyComponent>(entity);
        if (hc && hc->parent != core::ecs::kInvalidEntity)
            worldTr = core::ecs::computeWorldTransform(world, entity);

        GpuLight gpu{};

        const uint32_t lightType = static_cast<uint32_t>(light.type);

        // color.rgb = light.color * intensity
        gpu.color[0] = light.color[0] * light.intensity;
        gpu.color[1] = light.color[1] * light.intensity;
        gpu.color[2] = light.color[2] * light.intensity;
        gpu.color[3] = std::cos(light.innerConeAngle); // innerCosAngle

        // spotAngles
        const float cosInner = std::cos(light.innerConeAngle);
        const float cosOuter = std::cos(light.outerConeAngle);
        gpu.spotAngles[0] = cosOuter;
        gpu.spotAngles[1] = 1.0f / (cosInner - cosOuter + 0.0001f);
        gpu.spotAngles[2] = -1.0f; // no shadow map
        gpu.spotAngles[3] = 0.0f;

        if (light.type == Light::Type::Directional) {
            // position unused for directional; w = type = 0
            gpu.position[0] = 0.0f;
            gpu.position[1] = 0.0f;
            gpu.position[2] = 0.0f;
            gpu.position[3] = 0.0f; // type = Directional

            // Forward direction: +Z in local space, rotated into world space
            const core::math::Vec3 forward =
                core::math::rotate(worldTr.rotation, core::math::Vec3{0.0f, 0.0f, 1.0f});
            gpu.direction[0] = forward.x;
            gpu.direction[1] = forward.y;
            gpu.direction[2] = forward.z;
            gpu.direction[3] = 1000.0f; // range (unused for directional but stored)
        } else {
            // Point or Spot
            gpu.position[0] = worldTr.position.x;
            gpu.position[1] = worldTr.position.y;
            gpu.position[2] = worldTr.position.z;
            gpu.position[3] = static_cast<float>(lightType); // type

            const core::math::Vec3 forward =
                core::math::rotate(worldTr.rotation, core::math::Vec3{0.0f, 0.0f, 1.0f});
            gpu.direction[0] = forward.x;
            gpu.direction[1] = forward.y;
            gpu.direction[2] = forward.z;
            gpu.direction[3] = light.range; // range
        }

        static_assert(sizeof(GpuLight) == 64);
        std::memcpy(result.bytes + result.count * 64, &gpu, sizeof(GpuLight));
        ++result.count;
    }

    return result;
}

} // namespace engine::rendering
