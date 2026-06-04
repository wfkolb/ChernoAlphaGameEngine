#pragma once
#include <core/components/Transform.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace engine::networking {

// 4-sample ring buffer for client-side entity interpolation.
// Introduces kDelayMs latency so there are always >=2 samples to lerp between.
// Extrapolates linearly up to kMaxExtrapMs beyond the latest sample.
class SnapshotBuffer {
public:
    static constexpr int   kSlots       = 4;
    static constexpr float kDelayMs     = 100.0f;
    static constexpr float kMaxExtrapMs = 200.0f;

    struct EntityState {
        uint32_t                 netId;
        engine::core::Transform  transform;
    };

    // Push a snapshot received at receivedTimeMs (milliseconds since app start).
    void push(uint32_t seq, float receivedTimeMs,
              const std::vector<EntityState>& states);

    // Sample the interpolated state at (currentTimeMs - kDelayMs).
    // The visitor is called once per entity in the interpolated frame.
    // Returns false if there is insufficient data to produce a sample.
    using Visitor = std::function<void(uint32_t netId,
                                       const engine::core::Transform&)>;
    bool sample(float currentTimeMs, const Visitor& visitor) const;

    void clear() noexcept;

private:
    struct Entry {
        uint32_t                  seq      = 0u;
        float                     timeMs   = 0.0f;
        std::vector<EntityState>  states;
        bool                      valid    = false;
    };

    Entry entries_[kSlots];
    int   head_ = 0;
};

} // namespace engine::networking
