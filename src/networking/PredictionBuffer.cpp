#include <networking/PredictionBuffer.h>
#include <cmath>

namespace engine::networking {

void PredictionBuffer::push(const engine::core::input::InputFrame& frame,
                             const engine::core::Transform& predicted) {
    const uint32_t idx = head_ % static_cast<uint32_t>(kSlots);
    slots_[idx].frame     = frame;
    slots_[idx].predicted = predicted;
    slots_[idx].valid     = true;
    ++head_;
}

bool PredictionBuffer::reconcile(uint32_t tick,
                                  const engine::core::Transform& serverTransform,
                                  engine::core::Transform*        outServer) const {
    const Slot* s = get(tick);
    if (!s || !s->valid) return false;

    const float dx = serverTransform.position.x - s->predicted.position.x;
    const float dy = serverTransform.position.y - s->predicted.position.y;
    const float dz = serverTransform.position.z - s->predicted.position.z;
    const float distSq = dx*dx + dy*dy + dz*dz;
    const float thresh = kReconcileThreshold * kReconcileThreshold;

    if (distSq > thresh) {
        if (outServer) *outServer = serverTransform;
        return true;
    }
    return false;
}

const PredictionBuffer::Slot* PredictionBuffer::get(uint32_t tick) const noexcept {
    for (int i = 0; i < kSlots; ++i) {
        const Slot& s = slots_[i];
        if (s.valid && s.frame.tick == tick) return &s;
    }
    return nullptr;
}

void PredictionBuffer::reset() noexcept {
    for (auto& s : slots_) s.valid = false;
    head_ = 0u;
}

} // namespace engine::networking
