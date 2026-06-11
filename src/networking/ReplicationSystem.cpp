#include <networking/ReplicationSystem.h>
#include <core/Profiler.h>
#include <cmath>
#include <algorithm>

namespace engine::networking {

// ── Transform encoding helpers ────────────────────────────────────────────────
// 19 bytes: pos xyz (3×f32=12) + rot smallest-three (3×s16=6) + rotIdx (u8=1)

static constexpr float kRotScale = 32767.0f / 0.7072f;

static void encodeTransform(ByteWriter& bw,
                             const engine::core::Transform& t) {
    bw.writeF32(t.position.x);
    bw.writeF32(t.position.y);
    bw.writeF32(t.position.z);

    float qx = t.rotation.x, qy = t.rotation.y,
          qz = t.rotation.z, qw = t.rotation.w;

    // Find the component with the largest absolute value.
    float absX = std::fabs(qx), absY = std::fabs(qy),
          absZ = std::fabs(qz), absW = std::fabs(qw);
    uint8_t largest = 0;
    float   maxVal  = absX;
    if (absY > maxVal) { maxVal = absY; largest = 1; }
    if (absZ > maxVal) { maxVal = absZ; largest = 2; }
    if (absW > maxVal) {                largest = 3; }
    (void)maxVal;

    // Ensure the omitted component would be positive so the sign is implicit.
    const float sign =
        (largest == 0 && qx < 0.0f) || (largest == 1 && qy < 0.0f) ||
        (largest == 2 && qz < 0.0f) || (largest == 3 && qw < 0.0f) ? -1.0f : 1.0f;
    qx *= sign; qy *= sign; qz *= sign; qw *= sign;

    float a = 0.0f, b = 0.0f, c = 0.0f;
    switch (largest) {
    case 0: a = qy; b = qz; c = qw; break;
    case 1: a = qx; b = qz; c = qw; break;
    case 2: a = qx; b = qy; c = qw; break;
    default: a = qx; b = qy; c = qz; break;
    }

    auto clampS16 = [](float v) -> int16_t {
        return static_cast<int16_t>(
            std::clamp(v * kRotScale, -32767.0f, 32767.0f));
    };
    bw.writeU16(static_cast<uint16_t>(clampS16(a)));
    bw.writeU16(static_cast<uint16_t>(clampS16(b)));
    bw.writeU16(static_cast<uint16_t>(clampS16(c)));
    bw.writeU8(largest);
}

[[maybe_unused]] static engine::core::Transform decodeTransform(ByteReader& br) {
    engine::core::Transform t;
    t.position.x = br.readF32();
    t.position.y = br.readF32();
    t.position.z = br.readF32();

    const float a = static_cast<float>(static_cast<int16_t>(br.readU16())) / kRotScale;
    const float b = static_cast<float>(static_cast<int16_t>(br.readU16())) / kRotScale;
    const float c = static_cast<float>(static_cast<int16_t>(br.readU16())) / kRotScale;
    const uint8_t idx = br.readU8();

    const float d = std::sqrt(std::max(0.0f, 1.0f - a*a - b*b - c*c));
    switch (idx) {
    case 0: t.rotation.x = d; t.rotation.y = a; t.rotation.z = b; t.rotation.w = c; break;
    case 1: t.rotation.x = a; t.rotation.y = d; t.rotation.z = b; t.rotation.w = c; break;
    case 2: t.rotation.x = a; t.rotation.y = b; t.rotation.z = d; t.rotation.w = c; break;
    default: t.rotation.x = a; t.rotation.y = b; t.rotation.z = c; t.rotation.w = d; break;
    }
    return t;
}

// ── Health encoding (5 bytes) ─────────────────────────────────────────────────

static void encodeHealth(ByteWriter& bw, const engine::core::Health& h) {
    bw.writeU16(static_cast<uint16_t>(std::clamp(h.currentHp,     0.0f, 65535.0f)));
    bw.writeU16(static_cast<uint16_t>(std::clamp(h.maxHp,         0.0f, 65535.0f)));
    bw.writeU8 (static_cast<uint8_t> (std::clamp(h.shieldPercent * 255.0f, 0.0f, 255.0f)));
}

// ── ReplicationSystem ─────────────────────────────────────────────────────────

void ReplicationSystem::init() {
    accumulator_ = 0.0f;
    seq_         = 0u;
    ackedSeq_    = 0u;
    tick_        = 0u;
    hasLatest_   = false;
    latestData_.clear();
}

bool ReplicationSystem::tick(engine::core::ecs::World& world, float dt) {
    accumulator_ += dt;
    ++tick_;
    constexpr float kInterval = 1.0f / kSnapshotHz;
    if (accumulator_ < kInterval) return false;
    accumulator_ -= kInterval;
    buildSnapshot(world);
    return true;
}

void ReplicationSystem::buildSnapshot(engine::core::ecs::World& world) {
    PROFILE_SCOPE("ReplicationSystem::buildSnapshot");
    ++seq_;

    // Count entities with NetworkIdentity.
    uint16_t entityCount = 0u;
    world.forEachEntity([&](engine::core::ecs::Entity e) {
        if (world.tryGet<NetworkIdentity>(e)) ++entityCount;
    });

    ByteWriter bw;
    // 19-byte header
    bw.writeU32(seq_);
    bw.writeU32(tick_);
    bw.writeF32(static_cast<float>(tick_) / 64.0f);
    bw.writeU16(entityCount);
    bw.writeU32(ackedSeq_);
    bw.writeU8(0u);  // flags

    // Per-entity payload
    world.forEachEntity([&](engine::core::ecs::Entity e) {
        const auto* ni = world.tryGet<NetworkIdentity>(e);
        if (!ni) return;

        const uint32_t rcb = ni->replicatedComponents;
        uint8_t dirtyMask  = 0u;
        if (rcb & RCB_TRANSFORM) dirtyMask |= static_cast<uint8_t>(RCB_TRANSFORM);
        if (rcb & RCB_HEALTH)    dirtyMask |= static_cast<uint8_t>(RCB_HEALTH);

        // 5-byte entity header
        bw.writeU32(ni->netId);
        bw.writeU8(dirtyMask);

        // Transform (19 bytes)
        if (dirtyMask & RCB_TRANSFORM) {
            const auto* tr = world.tryGet<engine::core::Transform>(e);
            const engine::core::Transform identity{};
            encodeTransform(bw, tr ? *tr : identity);
        }
        // Health (5 bytes)
        if (dirtyMask & RCB_HEALTH) {
            const auto* h = world.tryGet<engine::core::Health>(e);
            const engine::core::Health zero{};
            encodeHealth(bw, h ? *h : zero);
        }
    });

    latestData_.assign(bw.data(), bw.data() + bw.byteSize());
    hasLatest_  = true;
}

void ReplicationSystem::acknowledgeSnapshot(uint32_t seq) noexcept {
    if (seq > ackedSeq_) ackedSeq_ = seq;
}

void ReplicationSystem::recordTransforms(engine::core::ecs::World& world,
                                          uint32_t serverTick) {
    const uint32_t slot = serverTick % kHistoryTicks;
    TransformHistoryEntry& entry = historyRing_[slot];
    entry.tick = serverTick;
    entry.transforms.clear();

    world.forEachEntity([&](engine::core::ecs::Entity e) {
        const auto* ni = world.tryGet<NetworkIdentity>(e);
        if (!ni) return;
        const auto* tr = world.tryGet<engine::core::Transform>(e);
        if (!tr) return;
        entry.transforms[ni->netId] = *tr;
    });
}

const TransformHistoryEntry* ReplicationSystem::getSnapshotAtTick(
    uint32_t tick) const noexcept {
    const uint32_t slot = tick % kHistoryTicks;
    const TransformHistoryEntry& entry = historyRing_[slot];
    // Guard against aliased stale data: the slot is valid only if it actually
    // holds data for the requested tick (not a different tick that maps to the
    // same slot, and not an uninitialised slot whose tick_ == 0).
    if (entry.tick != tick || entry.transforms.empty()) return nullptr;
    return &entry;
}

bool ReplicationSystem::decodeHeader(SnapshotHeader& out) const {
    PROFILE_SCOPE("ReplicationSystem::decodeHeader");
    if (latestData_.size() < 19u) return false;
    ByteReader br(latestData_.data(), latestData_.size());
    out.seq         = br.readU32();
    out.tick        = br.readU32();
    out.serverTime  = br.readF32();
    out.entityCount = br.readU16();
    out.ackedSeq    = br.readU32();
    out.flags       = br.readU8();
    return br.ok();
}

} // namespace engine::networking
