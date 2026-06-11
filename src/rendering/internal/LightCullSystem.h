#pragma once
#include <cstdint>

// Forward declaration only — avoids pulling in heavy ECS headers.
namespace engine { namespace core { namespace ecs { class World; } } }

namespace engine::rendering {

// Iterates all entities with Transform + Light components and fills a
// per-frame GpuLight array. Called before snapshot/cull each frame.
// Returns the number of lights packed (capped at kMaxLights = 64).
struct GpuLightData {
    // Raw bytes for kMaxLights GpuLight structs. Caller uploads to GPU.
    // GpuLight is 64 bytes; total = 64 * 64 = 4096 bytes.
    static constexpr uint32_t kMaxLights = 64;
    uint8_t  bytes[kMaxLights * 64];
    uint32_t count;
};

GpuLightData buildLightArray(core::ecs::World& world) noexcept;

} // namespace engine::rendering
