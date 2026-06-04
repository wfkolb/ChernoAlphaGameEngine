#include <networking/SnapshotBuffer.h>
#include <algorithm>

namespace engine::networking {

void SnapshotBuffer::push(uint32_t seq, float receivedTimeMs,
                           const std::vector<EntityState>& states) {
    Entry& e   = entries_[head_ % kSlots];
    e.seq      = seq;
    e.timeMs   = receivedTimeMs;
    e.states   = states;
    e.valid    = true;
    head_ = (head_ + 1) % kSlots;
}

bool SnapshotBuffer::sample(float currentTimeMs, const Visitor& visitor) const {
    const float targetMs = currentTimeMs - kDelayMs;

    // Find the two entries bracketing targetMs.
    const Entry* prev = nullptr;
    const Entry* next = nullptr;

    for (int i = 0; i < kSlots; ++i) {
        const Entry& e = entries_[i];
        if (!e.valid) continue;
        if (e.timeMs <= targetMs) {
            if (!prev || e.timeMs > prev->timeMs) prev = &e;
        } else {
            if (!next || e.timeMs < next->timeMs) next = &e;
        }
    }

    if (!prev) return false;  // no data at or before target

    if (!next) {
        // Extrapolation: use latest sample if within kMaxExtrapMs
        if (targetMs - prev->timeMs > kMaxExtrapMs) return false;
        for (const auto& es : prev->states)
            visitor(es.netId, es.transform);
        return true;
    }

    // Interpolation
    const float span  = next->timeMs - prev->timeMs;
    const float alpha = (span > 0.0f) ? (targetMs - prev->timeMs) / span : 0.0f;

    for (const auto& pa : prev->states) {
        // Find matching entity in next snapshot
        const EntityState* na = nullptr;
        for (const auto& nb : next->states) {
            if (nb.netId == pa.netId) { na = &nb; break; }
        }
        if (!na) {
            visitor(pa.netId, pa.transform);
            continue;
        }
        engine::core::Transform lerped;
        const auto& p = pa.transform.position;
        const auto& n = na->transform.position;
        lerped.position.x = p.x + (n.x - p.x) * alpha;
        lerped.position.y = p.y + (n.y - p.y) * alpha;
        lerped.position.z = p.z + (n.z - p.z) * alpha;
        lerped.rotation   = alpha < 0.5f ? pa.transform.rotation : na->transform.rotation;
        lerped.scale      = pa.transform.scale;
        visitor(pa.netId, lerped);
    }
    return true;
}

void SnapshotBuffer::clear() noexcept {
    for (auto& e : entries_) e.valid = false;
    head_ = 0;
}

} // namespace engine::networking
