# 02 — Game Mode: `IGameMode` and Round Lifecycle

`IGameMode` is the primary extension point for round and match logic. Implement one concrete subclass per game mode and install it through `GameLoop`.

## The `IGameMode` interface

```cpp
// src/app/public/app/IGameMode.h
namespace engine::app {

class IGameMode {
public:
    virtual ~IGameMode() = default;

    virtual void onRoundStart(GameContext& ctx, uint32_t roundNumber) {}
    virtual void onRoundTick(GameContext& ctx, float dt, uint32_t tick) {}
    virtual void onRoundEnd(GameContext& ctx, RoundResult result) {}
    virtual void onMatchEnd(GameContext& ctx, MatchResult result) {}

    virtual void onPlayerJoin(GameContext& ctx, PlayerId pid) {}
    virtual void onPlayerLeave(GameContext& ctx, PlayerId pid) {}

    // Return the spawn transform the engine should use for this player.
    virtual engine::core::math::Transform onPlayerSpawn(
        GameContext& ctx, PlayerId pid) = 0;

    // Called when a player entity's Health reaches zero.
    virtual void onPlayerDeath(
        GameContext& ctx, PlayerId victim, PlayerId killer) = 0;

    // Called every network tick (~20 Hz). Must produce ≤ 256 bytes.
    virtual void serializeState(engine::app::BitStreamWriter& w) const = 0;
    virtual void deserializeState(engine::app::BitStreamReader& r)     = 0;

    // Return a non-null value to end the round this tick.
    virtual std::optional<RoundResult> evaluateWinCondition(
        const GameContext& ctx) const = 0;
};

} // namespace engine::app
```

| Hook | When it runs |
|------|-------------|
| `onRoundStart` | After map load, before first tick |
| `onRoundTick` | Every fixed 64 Hz tick while round is live |
| `onPlayerJoin` | When a client finishes connecting |
| `onPlayerSpawn` | Whenever the engine needs a spawn transform for a player |
| `onPlayerDeath` | Immediately after a player entity's HP hits zero |
| `evaluateWinCondition` | End of every game tick; return a result to end the round |
| `onRoundEnd` | When `evaluateWinCondition` returns a non-null result |
| `onMatchEnd` | After all rounds are complete |
| `serializeState` | Every 3rd tick (~20 Hz); fills `GameModeStateBlob` in snapshot |
| `deserializeState` | Client-side, on each received snapshot |

## Round result types

```cpp
struct RoundResult {
    uint8_t     winningTeam = 0xFF;   // 0xFF = draw
    std::string reason;               // "kill_limit", "time_expired", etc.
    bool        isTechnical = false;  // server error / empty server
};

struct MatchResult {
    uint8_t     winningTeam   = 0xFF;
    int         roundsWon[2]  = {};
    int         totalKills[2] = {};
    float       matchDuration = 0.f;
    std::string mapName;
    std::string modeName;
};
```

## Implementing team deathmatch

```cpp
// TDMGameMode.h
#pragma once
#include <app/IGameMode.h>

class TDMGameMode : public engine::app::IGameMode {
public:
    static constexpr int   kKillsToWin   = 30;
    static constexpr float kRoundTimeSec = 600.f;  // 10 minutes
    static constexpr float kRespawnDelay = 5.f;

    void onRoundStart(engine::app::GameContext& ctx,
                      uint32_t round) override;

    engine::core::math::Transform onPlayerSpawn(
        engine::app::GameContext& ctx,
        engine::app::PlayerId pid) override;

    void onPlayerDeath(engine::app::GameContext& ctx,
                       engine::app::PlayerId victim,
                       engine::app::PlayerId killer) override;

    void onRoundTick(engine::app::GameContext& ctx,
                     float dt, uint32_t tick) override;

    std::optional<engine::app::RoundResult> evaluateWinCondition(
        const engine::app::GameContext& ctx) const override;

    void serializeState(engine::app::BitStreamWriter& w) const override;
    void deserializeState(engine::app::BitStreamReader& r) override;

    int   getTeamKills(int team) const { return teamKills_[team]; }
    float getTimeRemaining() const     { return timeRemaining_; }

private:
    uint8_t getPlayerTeam(engine::app::PlayerId pid) const;
    void    scheduleRespawn(engine::app::GameContext& ctx,
                            engine::app::PlayerId pid, float delay);

    int   teamKills_[2]{};
    float timeRemaining_ = 0.f;
};
```

```cpp
// TDMGameMode.cpp
#include "TDMGameMode.h"
#include <core/scene/SceneManager.h>

void TDMGameMode::onRoundStart(engine::app::GameContext& ctx, uint32_t) {
    teamKills_[0] = teamKills_[1] = 0;
    timeRemaining_ = kRoundTimeSec;
}

engine::core::math::Transform TDMGameMode::onPlayerSpawn(
    engine::app::GameContext& ctx, engine::app::PlayerId pid)
{
    uint8_t team = getPlayerTeam(pid);
    // Retrieve spawn points from the active scene's SceneGlobals.
    const auto& spawnPts = ctx.sceneManager.getActive()->globals.spawnPoints;
    // Even indices → team 0; odd indices → team 1.
    for (size_t i = team; i < spawnPts.size(); i += 2) {
        // Pick the first unoccupied spawn for this team (simplified).
        return spawnPts[i];
    }
    return {};  // fallback: origin
}

void TDMGameMode::onPlayerDeath(engine::app::GameContext& ctx,
                                engine::app::PlayerId victim,
                                engine::app::PlayerId killer)
{
    if (killer != engine::app::kInvalidPlayerId) {
        ++teamKills_[getPlayerTeam(killer)];
    }
    scheduleRespawn(ctx, victim, kRespawnDelay);
}

void TDMGameMode::onRoundTick(engine::app::GameContext& ctx,
                              float dt, uint32_t)
{
    timeRemaining_ -= dt;
}

std::optional<engine::app::RoundResult> TDMGameMode::evaluateWinCondition(
    const engine::app::GameContext&) const
{
    for (int t = 0; t < 2; ++t) {
        if (teamKills_[t] >= kKillsToWin) {
            return engine::app::RoundResult{
                .winningTeam = static_cast<uint8_t>(t),
                .reason      = "kill_limit"
            };
        }
    }
    if (timeRemaining_ <= 0.f) {
        uint8_t winner = (teamKills_[0] >= teamKills_[1]) ? 0u : 1u;
        return engine::app::RoundResult{
            .winningTeam = winner,
            .reason      = "time_expired"
        };
    }
    return std::nullopt;
}
```

## Serializing game-mode state for clients

The engine snapshots `GameModeStateBlob` (~20 Hz) and sends it to all clients. Keep it under 256 bytes. The client calls `deserializeState` whenever the blob changes.

```cpp
void TDMGameMode::serializeState(engine::app::BitStreamWriter& w) const {
    w.writeU16(static_cast<uint16_t>(teamKills_[0]));
    w.writeU16(static_cast<uint16_t>(teamKills_[1]));
    w.writeF32(timeRemaining_);
}

void TDMGameMode::deserializeState(engine::app::BitStreamReader& r) {
    int old0 = teamKills_[0];
    teamKills_[0]  = r.readU16();
    teamKills_[1]  = r.readU16();
    timeRemaining_ = r.readF32();

    // Fire local events when something interesting changed.
    if (teamKills_[0] != old0) {
        // e.g. update kill-feed HUD
    }
}
```

## Wiring the game mode into the application

Install the game mode inside `IGame::onInit`:

```cpp
void ArenaGame::onInit(engine::app::GameContext& ctx) {
    ctx.gameLoop.setGameMode(&gameMode_);

    engine::core::fps::registerFpsArchetypes(ctx.entityFactory);

    ctx.scheduler.registerSystem(engine::app::TickGroup::GameFixed,
        [this, &ctx](float dt) {
            gameMode_.onRoundTick(ctx, dt, ctx.currentTick);
        });
}
```

## Round lifecycle

```
onInit()
  └─► GameMode::onRoundStart(ctx, round=1)
        └─► Players connect → onPlayerJoin() → onPlayerSpawn()
              └─► [64 Hz game ticks]
                    └─► evaluateWinCondition() returns RoundResult
                          └─► onRoundEnd()
                                └─► onRoundStart(ctx, round=2)
                                      ...
                                        └─► onMatchEnd()
```

After `onRoundEnd` the engine applies a freeze period (`kPostRoundFreezeSec` = 3 s) before starting the next round. Player movement is disabled during the freeze; `onRoundTick` is not called.

## Next

[03 — Scenes](03_scenes.md): Creating, loading, and managing `.scene` files.
