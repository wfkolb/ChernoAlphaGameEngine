#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::tools {

// Immutable archive of a completed match, written once at match end.
// Stored as `<matchId-hex>.matchrecord`; never overwritten.
struct MatchRecord {
    struct PlayerRecord {
        uint64_t    accountId    = 0;
        std::string displayName;
        uint8_t     team         = 0;
        uint32_t    kills        = 0;
        uint32_t    deaths       = 0;
        uint32_t    assists      = 0;
        float       accuracy     = 0.f;       // shotsHit / shotsFired in [0,1]
        float       damageDealt  = 0.f;
        float       damageTaken  = 0.f;
        float       timeAliveSec = 0.f;
        bool        wasPresent   = true;
    };

    // Match identity
    uint64_t    matchId       = 0;
    uint64_t    startUnixTime = 0;
    float       durationSec   = 0.f;
    std::string mapName;
    std::string gameModeName;
    uint32_t    serverVersion = 0;

    // Outcome
    uint8_t winningTeam  = 0xFF;              // 0xFF = draw
    int32_t roundsPlayed = 0;
    int32_t roundsWon[2] = {};

    std::vector<PlayerRecord> players;

    // Game-mode-specific opaque blob (max 512 bytes).
    std::vector<uint8_t> gameModeRecord;
};

} // namespace engine::tools
