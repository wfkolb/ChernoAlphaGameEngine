#pragma once
#include <networking/NetworkIdentity.h>
#include <networking/ReplicatedComponentBit.h>
#include <networking/BitWriter.h>
#include <networking/BitReader.h>
#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <cstdint>
#include <vector>

namespace engine::networking {

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

    void init();

    // Advance time by dt. Returns true when a new snapshot is produced (~20 Hz).
    bool tick(engine::core::ecs::World& world, float dt);

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

    void buildSnapshot(engine::core::ecs::World& world);
};

} // namespace engine::networking
