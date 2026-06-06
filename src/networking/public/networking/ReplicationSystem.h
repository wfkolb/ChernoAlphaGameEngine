#pragma once
#include <networking/NetworkIdentity.h>
#include <networking/ReplicatedComponentBit.h>
#include <networking/BitWriter.h>
#include <networking/BitReader.h>
#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::networking {

// One slot in the server-side transform history ring.
// Populated each server tick via recordTransforms(); used by DamageSystem for
// lag-compensation rewind.
struct TransformHistoryEntry {
    uint32_t tick = 0u;
    // netId → Transform at this tick
    std::unordered_map<uint32_t, engine::core::Transform> transforms;
};

// Builds delta-compressed entity snapshots at ~20 Hz (every 3 fixed ticks).
// Wire format per snapshot:
//   Header (19 bytes): seq u32, tick u32, serverTime f32, entityCount u16,
//                      ackedSeq u32, flags u8
//   Per entity (5 + variable):
//     Entity header (5 bytes): netId u32, dirtyMask u8
//     Transform (19 bytes, if RCB_TRANSFORM): pos xyz f32, rot xyz s16, rotIdx u8
//     Health    ( 5 bytes, if RCB_HEALTH):    currentHp u16, maxHp u16, shield u8
class ReplicationSystem {
public:
    static constexpr int   kRingSize   = 32;
    static constexpr float kSnapshotHz = 20.0f;

    // Number of server ticks of transform history retained for lag compensation.
    // 64 ticks at 64 Hz = 1 second, covering all realistic client latencies.
    static constexpr uint32_t kHistoryTicks = 64u;

    void init();

    // Advance time by dt. Returns true when a new snapshot is produced (~20 Hz).
    bool tick(engine::core::ecs::World& world, float dt);

    // Record all networked entity transforms for `serverTick`. Call once per
    // server physics tick (64 Hz) to keep the history ring current.
    // Only entities with NetworkIdentity + Transform are stored.
    void recordTransforms(engine::core::ecs::World& world, uint32_t serverTick);

    // Return the history entry for `tick`, or nullptr if `tick` is older than
    // the oldest retained entry (more than kHistoryTicks ticks ago).
    const TransformHistoryEntry* getSnapshotAtTick(uint32_t tick) const noexcept;

    // Decode the latest snapshot header fields for inspection.
    struct SnapshotHeader {
        uint32_t seq         = 0;
        uint32_t tick        = 0;
        float    serverTime  = 0.0f;
        uint16_t entityCount = 0;
        uint32_t ackedSeq    = 0;
        uint8_t  flags       = 0;
    };
    bool decodeHeader(SnapshotHeader& out) const;

    // Acknowledge that the peer received snapshot with given sequence number.
    void acknowledgeSnapshot(uint32_t seq) noexcept;

    uint32_t currentSequence() const noexcept { return seq_; }
    bool     hasSnapshot()     const noexcept { return hasLatest_; }

    // Raw bytes of the most recently built snapshot.
    const std::vector<uint8_t>& latestData() const noexcept { return latestData_; }

private:
    float    accumulator_ = 0.0f;
    uint32_t seq_         = 0u;
    uint32_t ackedSeq_    = 0u;
    uint32_t tick_        = 0u;
    bool     hasLatest_   = false;

    std::vector<uint8_t> latestData_;

    // Ring buffer of transform snapshots, one per server tick.
    // Slot index = tick % kHistoryTicks.
    std::array<TransformHistoryEntry, kHistoryTicks> historyRing_ = {};

    void buildSnapshot(engine::core::ecs::World& world);
};

} // namespace engine::networking
