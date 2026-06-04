# 09 — Persistence: `SaveSystem`, Profiles, and Checkpoints

Persistence in a multiplayer FPS covers four distinct concerns, each with a different lifetime and owner:

| Concern | Owner | Format |
|---------|-------|--------|
| Player profile | Server (per account) | `.profile` — LZ4 + CRC32 |
| Match record | Server (written at match end) | `.matchrecord` binary |
| Server checkpoint | Server (periodic / round end) | `.checkpoint` binary |
| Client settings | Client (embedded in profile) | Part of `.profile` |

The `SaveSystem` is exposed through `GameContext::saveSystem`. All file I/O uses atomic write-then-rename (`.tmp` swap) to prevent corruption on crash.

## `SaveSystem` API

```cpp
// src/tools/public/tools/SaveSystem.h
namespace engine::tools {

class SaveSystem {
public:
    // --- Player profiles ---

    // Returns a default-constructed profile if the file does not exist.
    PlayerProfile loadProfile(uint64_t accountId);

    // Writes atomically. Thread-safe; can be called from onShutdown.
    void saveProfile(const PlayerProfile& profile);

    // --- Match records ---

    // Appends a match record. Does not block the game loop; writes async.
    void writeMatchRecord(const MatchRecord& record);

    // --- Checkpoints ---

    // Write a checkpoint of the current world state.
    // Runs on a background thread; safe to call during PostPhysics.
    void createCheckpoint(const engine::core::ecs::World& world,
                          const engine::core::scene::SceneGlobals& globals);

    // Load a checkpoint back into a World (call before activate()).
    bool loadCheckpoint(std::string_view path,
                        engine::core::ecs::World& world,
                        engine::core::scene::SceneGlobals& globals);

    // Path configuration (set in tests or non-default deployments)
    void setRootPath(std::string_view root);
};

} // namespace engine::tools
```

## Default file layout

```
<server root>/
  saves/
    profiles/
      <accountId>.profile
    matches/
      <matchId>.matchrecord
    checkpoints/
      <sceneName>_<tick>.checkpoint
      <sceneName>_latest.checkpoint   ← symlink/copy to most recent
```

Override the root path for tests:

```cpp
ctx.saveSystem.setRootPath("test_saves/");
```

## Player profile

### Loading at connect

```cpp
void ArenaGame::onPlayerJoin(engine::app::GameContext& ctx,
                              engine::app::PlayerId pid)
{
    engine::tools::PlayerProfile profile =
        ctx.saveSystem.loadProfile(getAccountId(pid));

    // Apply saved mouse sensitivity
    ctx.inputSystem.setAxisSensitivity("LookYaw",   profile.mouseSensitivity);
    ctx.inputSystem.setAxisSensitivity("LookPitch",
        profile.mouseSensitivity * (profile.invertPitchAxis ? -1.f : 1.f));

    // Apply saved key bindings
    if (!profile.bindingOverrides.empty())
        ctx.inputSystem.applyBindingOverrides(profile.bindingOverrides);

    // Load loadout
    equipLoadout(ctx, pid, profile.loadout);
}
```

### Updating stats at death

```cpp
void ArenaGame::onPlayerDeath(engine::app::GameContext& ctx,
                               engine::app::PlayerId victim,
                               engine::app::PlayerId killer)
{
    if (killer != engine::app::kInvalidPlayerId) {
        engine::tools::PlayerProfile killerProfile =
            ctx.saveSystem.loadProfile(getAccountId(killer));
        ++killerProfile.stats.kills;
        ctx.saveSystem.saveProfile(killerProfile);
    }

    engine::tools::PlayerProfile victimProfile =
        ctx.saveSystem.loadProfile(getAccountId(victim));
    ++victimProfile.stats.deaths;
    ctx.saveSystem.saveProfile(victimProfile);
}
```

### Saving at disconnect

```cpp
void ArenaGame::onPlayerLeave(engine::app::GameContext& ctx,
                               engine::app::PlayerId pid)
{
    engine::tools::PlayerProfile profile =
        ctx.saveSystem.loadProfile(getAccountId(pid));
    profile.lastSeenUnixTime  = currentUnixTime();
    profile.bindingOverrides  = ctx.inputSystem.serializeBindingOverrides();
    ctx.saveSystem.saveProfile(profile);
}
```

### `PlayerProfile` structure

```cpp
struct PlayerProfile {
    uint64_t    accountId         = 0;
    std::string displayName;                    // max 32 UTF-8 chars
    uint64_t    firstSeenUnixTime = 0;
    uint64_t    lastSeenUnixTime  = 0;

    struct LoadoutSlot {
        uint32_t weaponDefId = 0;   // index into game's weapon table
        uint32_t skinId      = 0;
    };
    LoadoutSlot loadout[3];          // primary, secondary, melee

    struct LifetimeStats {
        uint64_t kills           = 0;
        uint64_t deaths          = 0;
        uint64_t assists         = 0;
        uint64_t matchesPlayed   = 0;
        uint64_t matchesWon      = 0;
        float    totalPlaytimeSec = 0.f;
        float    totalDamageDealt = 0.f;
    } stats;

    // Client settings
    float mouseSensitivity   = 1.f;
    float adsSensitivityMult = 0.6f;
    float fovDegrees         = 90.f;
    bool  invertPitchAxis    = false;
    float masterVolume       = 1.f;

    // Opaque blobs — parsed by their respective subsystems
    std::vector<uint8_t> bindingOverrides;    // InputSystem format
    std::vector<uint8_t> gameExtensionData;   // game-defined; versioned separately
};
```

## Match records

Write a match record at `IGameMode::onMatchEnd`:

```cpp
void TDMGameMode::onMatchEnd(engine::app::GameContext& ctx,
                              const engine::app::MatchResult& result)
{
    engine::tools::MatchRecord rec;
    rec.matchId       = generateMatchId();
    rec.mapName       = ctx.sceneManager.getActive()->globals.sceneName;
    rec.gameModeName  = "TDM";
    rec.winningTeam   = result.winningTeam;

    for (const auto& [pid, data] : matchStats_) {
        rec.players.push_back({
            .accountId = getAccountId(pid),
            .kills     = data.kills,
            .deaths    = data.deaths,
            .assists   = data.assists,
        });
    }

    ctx.saveSystem.writeMatchRecord(rec);
}
```

Match records are immutable once written. They are not loaded at runtime by the engine; your game's backend or stats UI reads them separately.

## Server checkpoints

Checkpoints capture the full entity state and let the server recover mid-match after a crash or restart.

### Writing a checkpoint

The engine automatically writes a checkpoint at round end. You can also trigger one manually:

```cpp
// E.g. in onGameTick every kCheckpointIntervalSec seconds
if (timeSinceLastCheckpoint_ >= kCheckpointIntervalSec) {
    ctx.saveSystem.createCheckpoint(ctx.world,
        ctx.sceneManager.getActive()->globals);
    timeSinceLastCheckpoint_ = 0.f;
}
```

`createCheckpoint` returns immediately; the actual file I/O happens on a background thread using a snapshot of the ECS data taken at the call site.

### Restoring a checkpoint on server start

```cpp
// In WinMain, before calling Application::run()
engine::tools::SaveSystem saves;
saves.setRootPath("server_data/");

engine::core::ecs::World world;
engine::core::scene::SceneGlobals globals;

if (saves.loadCheckpoint("server_data/saves/checkpoints/arena_01_latest.checkpoint",
                          world, globals))
{
    ctx.sceneManager.activate(ctx.sceneManager.loadFromWorld(world, globals));
} else {
    // No checkpoint — fresh start
    ctx.sceneManager.activate(ctx.sceneManager.load("content/maps/arena_01.scene"));
}
```

## Versioning and forward compatibility

`.profile` and `.matchrecord` files include a schema version field. Unknown fields in a newer file are skipped when loading with an older engine build. New mandatory fields specify a default that old builds fill in. This means:

- Updating the profile schema does not invalidate existing saves.
- Removing a field from the schema does not cause parse errors — the bytes are skipped via the length-prefixed section format.
- LZ4 compression + CRC32 integrity check are always applied; a corrupt file returns an error from `loadProfile` and the caller should substitute a fresh default profile.

## Next

[10 — Editor & PIE](10_editor_pie.md): Using `EngineEditor`, scene authoring panels, and Play-in-Editor.
