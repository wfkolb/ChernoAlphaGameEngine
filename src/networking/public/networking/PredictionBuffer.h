#pragma once
#include <core/input/InputFrame.h>
#include <core/components/Transform.h>
#include <cstdint>

namespace engine::networking {

// 128-slot circular buffer storing per-tick InputFrame + predicted Transform.
// Used by clients to roll back and replay input after server reconciliation.
class PredictionBuffer {
public:
    static constexpr int   kSlots              = 128;
    static constexpr float kReconcileThreshold = 0.05f;  // metres

    struct Slot {
        engine::core::input::InputFrame frame;
        engine::core::Transform         predicted;
        bool                            valid = false;
    };

    // Store a prediction for the given tick.
    void push(const engine::core::input::InputFrame& frame,
              const engine::core::Transform& predicted);

    // Compare the server-authoritative transform at the given tick against our
    // prediction.  Returns true (reconcile needed) if distance > kReconcileThreshold.
    // *outServer is set to serverTransform when returning true.
    bool reconcile(uint32_t tick,
                   const engine::core::Transform& serverTransform,
                   engine::core::Transform*        outServer) const;

    // Return the slot for the given tick, or nullptr if evicted / not found.
    const Slot* get(uint32_t tick) const noexcept;

    void reset() noexcept;

private:
    Slot     slots_[kSlots] = {};
    uint32_t head_          = 0u;
};

} // namespace engine::networking
