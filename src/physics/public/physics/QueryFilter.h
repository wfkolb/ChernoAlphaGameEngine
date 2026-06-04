#pragma once

#include <array>
#include <cstdint>

namespace engine::physics {

constexpr int kMaxPhysicsLayers = 16;

// Symmetric 16×16 collision matrix encoded as per-layer bitmask.
// By default all layers collide with all other layers.
struct QueryFilter {
    std::array<uint16_t, kMaxPhysicsLayers> layerMask{};

    constexpr QueryFilter() noexcept {
        for (auto& m : layerMask) m = 0xFFFF;
    }

    constexpr bool collides(uint8_t layerA, uint8_t layerB) const noexcept {
        return (layerMask[layerA] & (static_cast<uint16_t>(1u) << layerB)) != 0;
    }

    constexpr void setCollides(uint8_t layerA, uint8_t layerB, bool value) noexcept {
        const auto bitB = static_cast<uint16_t>(1u) << layerB;
        const auto bitA = static_cast<uint16_t>(1u) << layerA;
        if (value) {
            layerMask[layerA] |= bitB;
            layerMask[layerB] |= bitA;
        } else {
            layerMask[layerA] &= static_cast<uint16_t>(~bitB);
            layerMask[layerB] &= static_cast<uint16_t>(~bitA);
        }
    }

    // Build a filter that only tests against a single layer.
    static constexpr QueryFilter singleLayer(uint8_t layer) noexcept {
        QueryFilter f;
        for (auto& m : f.layerMask) m = 0;
        f.layerMask[layer] = static_cast<uint16_t>(1u) << layer;
        return f;
    }
};

} // namespace engine::physics
