#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::tools {

// Per-account persistent record, stored server-side as `<accountId>.profile`.
// On disk: LZ4-compressed payload guarded by a CRC32, written atomically.
// Deserialization is forward-compatible (see SaveSystem).
struct PlayerProfile {
    struct LoadoutSlot {
        uint32_t weaponDefId = 0;
        uint32_t skinId      = 0;
    };

    struct LifetimeStats {
        uint64_t kills            = 0;
        uint64_t deaths           = 0;
        uint64_t assists          = 0;
        uint64_t matchesPlayed    = 0;
        uint64_t matchesWon       = 0;
        uint64_t roundsPlayed     = 0;
        uint64_t roundsWon        = 0;
        float    totalShotsFired  = 0.f;
        float    totalShotsHit    = 0.f;
        float    totalPlaytimeSec = 0.f;
        float    totalDamageDealt = 0.f;
    };

    // Identity
    uint64_t    accountId         = 0;
    std::string displayName;                  // max 32 UTF-8 chars
    uint64_t    firstSeenUnixTime = 0;
    uint64_t    lastSeenUnixTime  = 0;

    // Loadout: primary, secondary, melee
    LoadoutSlot loadout[3] = {};

    LifetimeStats stats;

    // Input/display settings (synced from client on connect)
    float mouseSensitivity   = 1.f;
    float adsSensitivityMult = 0.6f;
    float masterVolume       = 1.f;
    float effectsVolume      = 1.f;
    float fovDegrees         = 90.f;
    bool  invertPitchAxis    = false;

    // Opaque, game-managed blobs.
    std::vector<uint8_t> bindingOverrides;
    std::vector<uint8_t> gameExtensionData;
};

} // namespace engine::tools
