# Multiplayer Save and Persistence System

**Phase 6 — Engine Version 0.6.x**
**Module:** `engine::core` — `engine/core/SaveSystem.h`
**Audience:** Game developers implementing persistence, player profiles, match history, and server checkpointing.

---

## Table of Contents

1. [Persistence in a Multiplayer FPS Context](#1-persistence-in-a-multiplayer-fps-context)
2. [Player Profiles](#2-player-profiles)
3. [Match Records](#3-match-records)
4. [Server-Side Scene Checkpointing](#4-server-side-scene-checkpointing)
5. [Checkpoint File Format](#5-checkpoint-file-format)
6. [Reconnection and Mid-Match Join](#6-reconnection-and-mid-match-join)
7. [File Layout on Disk](#7-file-layout-on-disk)
8. [Versioning and Forward Compatibility](#8-versioning-and-forward-compatibility)
9. [C++ Save System API](#9-c-save-system-api)
10. [Integration Patterns and Best Practices](#10-integration-patterns-and-best-practices)

---

## 1. Persistence in a Multiplayer FPS Context

Traditional single-player save systems checkpoint the world mid-gameplay so a player can resume exactly where they left off. Multiplayer FPS games do not work this way. The authoritative simulation lives on the server; a client closing the game does not interrupt the server, and the world state is not meaningful outside of a running server instance.

Persistence in this engine therefore covers four distinct concerns:

| Concern | Lifetime | Owner | Format |
|---|---|---|---|
| **Player profile** | Persistent, per-account | Server (per-player file) | `.profile` binary |
| **Match record** | Written once at match end | Server | `.matchrecord` binary |
| **Server checkpoint** | Periodic, overwritten | Server | `.checkpoint` binary |
| **Client settings** | Persistent, per-machine | Client | Embedded in `.profile` |

The engine does not maintain a cloud service. Profiles and records are stored on the server machine's filesystem. A game targeting a hosted/ranked environment should layer their own backend sync (write to a database on match end, cache locally as a fallback) — the engine's save system provides the local file I/O; the network transport to a remote backend is game-side responsibility.

---

## 2. Player Profiles

### 2.1 Profile Contents

A player profile is a per-account persistent record that lives on the server. When a client connects, the server loads their profile by account ID. When the client disconnects, the profile is flushed to disk.

```cpp
struct PlayerProfile
{
    // Identity
    uint64_t    accountId       = 0;
    std::string displayName;             // max 32 UTF-8 chars
    uint64_t    firstSeenUnixTime = 0;
    uint64_t    lastSeenUnixTime  = 0;

    // Loadout (game-defined: which weapons the player has equipped)
    struct LoadoutSlot {
        uint32_t weaponDefId = 0;         // asset ID into game's weapon table
        uint32_t skinId      = 0;
    };
    LoadoutSlot loadout[3];              // primary, secondary, melee

    // Lifetime stats
    struct LifetimeStats {
        uint64_t kills         = 0;
        uint64_t deaths        = 0;
        uint64_t assists       = 0;
        uint64_t matchesPlayed = 0;
        uint64_t matchesWon    = 0;
        uint64_t roundsPlayed  = 0;
        uint64_t roundsWon     = 0;
        float    totalShotsFired   = 0.f;
        float    totalShotsHit     = 0.f;
        float    totalPlaytimeSec  = 0.f;
        float    totalDamageDealt  = 0.f;
    } stats;

    // Input/display settings (synced from client on connect)
    float    mouseSensitivity      = 1.f;
    float    adsSensitivityMult    = 0.6f;
    float    masterVolume          = 1.f;
    float    effectsVolume         = 1.f;
    float    fovDegrees            = 90.f;
    bool     invertPitchAxis       = false;

    // Input rebinding blob (opaque; parsed by InputSystem)
    std::vector<uint8_t> bindingOverrides;

    // Game-defined extension data (arbitrary bytes; versioned separately)
    std::vector<uint8_t> gameExtensionData;
};
```

### 2.2 Profile Lifecycle

```
Client connects
  └─► Server: SaveSystem::loadProfile(accountId)
        ├─ If file exists: load and deserialize
        └─ If not: create new profile with defaults

[Session active]
  └─► Server: profile is held in memory, updated on kills/events

Client disconnects / match ends
  └─► Server: SaveSystem::saveProfile(accountId, profile)
        └─► Writes .profile file atomically (write to .profile.tmp, rename)
```

Profiles are never held open for exclusive access. The load/save model is load-into-memory, mutate, write-back. This means multiple server processes could each maintain their own copy of a player's profile if a player is somehow connected to two servers simultaneously — the game layer must prevent this (e.g., global session token validation).

### 2.3 Stat Accumulation

Stats are accumulated during a match into a per-player `MatchStatsAccumulator` (a temporary in-memory struct), and committed to the profile at match end — not tick-by-tick. This prevents excessive file I/O and ensures stats are only recorded for completed matches.

```cpp
// At match end:
for (auto& [pid, accum] : matchStats) {
    PlayerProfile profile = ctx.save.loadProfile(pid);
    profile.stats.kills        += accum.kills;
    profile.stats.deaths       += accum.deaths;
    profile.stats.totalShotsFired += accum.shotsFired;
    profile.stats.totalShotsHit   += accum.shotsHit;
    profile.stats.matchesPlayed   += 1;
    if (accum.wonMatch) ++profile.stats.matchesWon;
    profile.lastSeenUnixTime = engine::unixTimeNow();
    ctx.save.saveProfile(pid, profile);
}
```

---

## 3. Match Records

A match record is written once, at match end, by the server. It is an immutable archive of the match outcome and per-player stats.

### 3.1 MatchRecord Contents

```cpp
struct MatchRecord
{
    // Match identity
    uint64_t    matchId        = 0;     // random 64-bit ID assigned at match start
    uint64_t    startUnixTime  = 0;
    float       durationSec    = 0.f;
    std::string mapName;
    std::string gameModeName;
    uint32_t    serverVersion  = 0;     // engine::kVersionPacked

    // Outcome
    uint8_t     winningTeam    = 0xFF;  // 0xFF = draw
    int         roundsPlayed   = 0;
    int         roundsWon[2]   = {};

    // Per-player stats
    struct PlayerRecord {
        uint64_t    accountId;
        std::string displayName;
        uint8_t     team;
        uint32_t    kills;
        uint32_t    deaths;
        uint32_t    assists;
        float       accuracy;           // shotsHit / shotsFired [0,1]
        float       damageDealt;
        float       damageTaken;
        float       timeAliveSec;
        bool        wasPresent;         // false if disconnected early
    };
    std::vector<PlayerRecord> players;

    // Game-mode-specific data blob (max 512 bytes)
    std::vector<uint8_t> gameModeRecord;
};
```

### 3.2 Writing a Match Record

```cpp
// In IGameMode::onMatchEnd (or directly in game code):
void TeamDeathmatchMode::onMatchEnd(engine::GameContext& ctx,
                                     engine::MatchResult result)
{
    engine::MatchRecord record{};
    record.matchId       = engine::generateRandomId();
    record.startUnixTime = matchStartTime_;
    record.durationSec   = result.matchDuration;
    record.mapName       = ctx.assets.currentMapName();
    record.gameModeName  = "team_deathmatch";
    record.serverVersion = engine::kVersionPacked;
    record.winningTeam   = result.winningTeam;
    record.roundsPlayed  = totalRounds_;

    for (auto& [pid, accum] : matchStats_) {
        engine::MatchRecord::PlayerRecord pr{};
        pr.accountId   = ctx.network.getAccountId(pid);
        pr.displayName = ctx.network.getDisplayName(pid);
        pr.team        = getPlayerTeam(pid);
        pr.kills       = accum.kills;
        pr.deaths      = accum.deaths;
        pr.assists     = accum.assists;
        pr.accuracy    = (accum.shotsFired > 0)
                           ? float(accum.shotsHit) / accum.shotsFired
                           : 0.f;
        pr.damageDealt = accum.damageDealt;
        pr.wasPresent  = ctx.network.isConnected(pid);
        record.players.push_back(pr);
    }

    ctx.save.writeMatchRecord(record);
}
```

### 3.3 Record Naming and Retention

Match records are named `<matchId>.matchrecord` and are never overwritten. The server accumulates them indefinitely; game developers should implement a pruning policy if disk space is a concern (e.g., delete records older than 90 days).

---

## 4. Server-Side Scene Checkpointing

### 4.1 When to Use Checkpoints

Most competitive FPS modes (round-based deathmatch, bomb defusal) do not benefit from mid-round checkpointing — a crashed server simply restarts the round. Checkpoints are relevant for:

- **Persistent worlds** (survival modes, persistent capture-point maps) where entity state is accumulated over many sessions and a crash must not reset it.
- **Long-duration round modes** where a mid-round server restart should resume rather than restart (e.g., a 20-minute bomb-defusal round where the bomb has already been planted).
- **Development / QA** — replaying a specific server state to reproduce a bug.

### 4.2 Checkpoint Trigger

Checkpoints can be triggered on a timer, on game-mode events, or manually:

```cpp
// Automatic periodic checkpointing (configured in server.toml)
[server.checkpoint]
enabled               = true
interval_game_ticks   = 12800    # every 200 seconds at 64 Hz
max_checkpoints_kept  = 3        # rotate: keep last 3 checkpoint files
```

```cpp
// Manual trigger from game mode (e.g., on bomb plant event):
void BombDefusalMode::onBombPlanted(engine::GameContext& ctx, engine::EntityId bombEid)
{
    ctx.save.writeCheckpoint();   // snapshot now so restart can resume mid-round
}
```

### 4.3 What Is Included in a Checkpoint

A checkpoint contains enough information to resume a running server session:

- All networked entity transforms, velocities, and component data registered with `NetworkedEntityComponent`
- The current game mode state blob (same data as the network snapshot's `GameModeStateBlob`)
- The current server tick counter and wall-clock timestamp
- The active map name (to reload the scene on resume)
- The connection list (account IDs of connected players; clients must reconnect)

A checkpoint does NOT include:
- Audio state
- Particle system state
- Non-networked entities (purely visual/client-side)
- Per-client network buffers (clients must re-handshake on resume)

---

## 5. Checkpoint File Format

### 5.1 Binary Layout

```
CheckpointFile {
    Header {
        magic         : uint32  = 0x454E4350  // 'ENCP'
        versionMajor  : uint16
        versionMinor  : uint16
        engineVersion : uint32  // engine::kVersionPacked
        gameVersion   : uint32  // game-defined, set via SaveSystem::setGameVersion()
        tickNumber    : uint64
        serverTimeSec : float64
        unixTimestamp : uint64
        mapNameLen    : uint16
        mapName       : uint8[mapNameLen]
        modeNameLen   : uint16
        modeName      : uint8[modeNameLen]
        checksum      : uint32  // CRC32 of entire file after header
    }
    EntityBlock {
        entityCount   : uint32
        entities[]    : CompressedEntityRecord
    }
    GameModeBlock {
        blobSize      : uint16
        blob          : uint8[blobSize]
    }
    PlayerListBlock {
        playerCount   : uint16
        players[] {
            accountId       : uint64
            lastKnownPingMs : uint16
        }
    }
    ExtensionBlock {        // for game-defined extra data
        blockSize     : uint32
        gameDataLen   : uint32
        gameData      : uint8[gameDataLen]
    }
}
```

### 5.2 Entity Compression

The `CompressedEntityRecord` is identical in structure to a full `WorldStateSnapshot` entity record (see `task-07-gameplay-loop.md`, Section 4.1) with the addition of all non-network-replicated components registered by the game. Components are serialized in registration order; unknown components are skipped on load using a per-component size field.

```
CompressedEntityRecord {
    netId           : uint32
    componentCount  : uint16
    components[] {
        typeHash    : uint32    // FNV-1a hash of the component type name
        dataSize    : uint16    // byte length of following data
        data        : uint8[dataSize]
    }
}
```

The entire entity block is compressed with LZ4 after serialization. Typical compression ratio for an FPS scene: 3–5x.

### 5.3 Checkpoint File Size

For a typical 10-player FPS map with ~500 networked entities:
- Uncompressed entity block: ~80 KB
- LZ4-compressed: ~20–30 KB
- Full checkpoint file: ~32 KB

Checkpoints are small enough to write synchronously without perceptible tick delay on server hardware. The write is performed on a background thread after the serialization pass; the engine double-buffers the serialized bytes to avoid holding the ECS world locked during file I/O.

---

## 6. Reconnection and Mid-Match Join

### 6.1 New Connection During an Active Match

When a client connects while a match is in progress:

1. Server sends a full (non-delta) `WorldStateSnapshot` as the first packet after the handshake.
2. Client receives the snapshot, populates its local ECS world, and begins interpolation.
3. Client's game mode calls `deserializeState()` with the current `GameModeStateBlob`, restoring score, round timer, and phase.
4. Client begins receiving delta snapshots normally from this point.

The round-trip from connection to first rendered frame is: handshake RTT + snapshot size / bandwidth. For a 32 KB snapshot over a 10 Mbps link, this is approximately 25 ms additional latency beyond the RTT.

### 6.2 Reconnect After Disconnect (Same Match)

When a previously connected player reconnects within the same match:

```
Client timeout detected by server (NetworkSystem::onClientTimeout(pid))
  └─► Mark client slot as "disconnected but reserving"
        └─► Hold reserved slot for kReconnectGracePeriodSec (default: 30 s)

Client reconnects within grace period:
  └─► Server: associate new connection with original PlayerId
        ├─► Restore player entity if it was despawned during disconnect
        ├─► Send full WorldStateSnapshot
        └─► Resume input stream normally

Grace period expires without reconnect:
  └─► Call GameMode::onPlayerLeave(pid)
        └─► Free player entity, remove slot reservation
```

The player entity is typically kept alive or frozen on the server during the grace period rather than immediately despawning, to avoid disrupting ongoing gameplay (e.g., a player disconnecting mid-combat should not cause the kill to vanish from the other player's perspective).

### 6.3 Checkpoint Resume (Server Restart)

When the server process crashes and is restarted with checkpoint resumption:

```bat
rem Restart with checkpoint resume
MyGame.exe --server --resume checkpoints/latest.checkpoint
```

```cpp
// In MyGame::onInit on server:
if (ctx.network.isServer() && ctx.save.hasCheckpointToResume()) {
    engine::CheckpointData cp = ctx.save.loadCheckpoint(ctx.save.resumeCheckpointPath());

    // Load the same map the checkpoint was taken on
    auto future = ctx.assets.loadSceneAsync(cp.mapName);
    future.waitForCompletion();
    ctx.world.instantiateScene(future.result());

    // Restore entity state from checkpoint
    ctx.save.applyCheckpointToWorld(cp, ctx.world);

    // Restore game mode state
    ctx.network.getGameMode()->deserializeState(cp.gameModeBlob);

    // Restore server tick counter (so lag compensation timestamps remain valid)
    ctx.network.setServerTick(cp.tickNumber);

    ctx.log.info("Resumed from checkpoint tick={} time={}",
                 cp.tickNumber, cp.serverTimeSec);
} else {
    // Normal startup
    ctx.assets.loadScene("content/maps/de_harbor.ascene", ...);
}
ctx.network.listenOnPort(7777);
```

Clients connecting to a resumed server go through the normal mid-match join flow (Section 6.1) — they receive a full snapshot and proceed normally. The checkpoint is transparent to clients.

---

## 7. File Layout on Disk

All persistence files are rooted at the **server data directory**, which defaults to a `serverdata/` folder adjacent to the game executable. This is configurable in `server.toml`:

```toml
[server.storage]
data_dir = "serverdata"     # relative to executable, or absolute path
```

```
serverdata/
├── profiles/
│   ├── 1001.profile            # profile for accountId 1001
│   ├── 1002.profile
│   └── 1001.profile.tmp        # atomic write staging file (cleaned up on startup)
│
├── matchrecords/
│   ├── 2026/
│   │   └── 04/
│   │       ├── A3F9B2C1.matchrecord
│   │       └── D72E5A8F.matchrecord
│   └── ...                    # year/month subdirectory organization
│
└── checkpoints/
    ├── latest.checkpoint       # symlink / copy of most recent
    ├── checkpoint_tick_00512000.checkpoint
    ├── checkpoint_tick_00499200.checkpoint
    └── checkpoint_tick_00486400.checkpoint  # 3 rotated checkpoints
```

### 7.1 Profile Path Resolution

Profiles are named by decimal account ID: `profiles/<accountId>.profile`. Account IDs are 64-bit unsigned integers. The directory is flat (no subdirectory sharding) up to ~100,000 files; if your game expects more than that, add a sharding level: `profiles/<accountId % 1000>/<accountId>.profile`.

### 7.2 Match Record Organization

Match records are organized into `year/month` subdirectories to bound directory entry count. Each file is named by the 64-bit match ID formatted as 8 uppercase hex digits.

### 7.3 Checkpoint Rotation

The engine keeps the last `max_checkpoints_kept` (default 3) checkpoint files and deletes the oldest when writing a new one. The file named `latest.checkpoint` is always a copy of (not a symlink to, for portability on Windows) the most recent checkpoint.

---

## 8. Versioning and Forward Compatibility

### 8.1 Version Fields

Every save file has two version fields in its header:

| Field | Meaning |
|---|---|
| `engineVersion` | Engine binary version at write time; packed as `(major << 16) | (minor << 8) | patch` |
| `gameVersion` | Game-defined version; set by the game developer via `SaveSystem::setGameVersion()` |

On load, the engine checks:
1. `magic` matches — if not, reject the file as corrupt or wrong format.
2. `engineVersion.major` matches current major — if not, emit a warning and attempt load anyway.
3. `checksum` matches the computed CRC32 — if not, reject the file as corrupt.

### 8.2 Component-Level Forward Compatibility

When loading a checkpoint, the engine uses the `typeHash` + `dataSize` fields in each `CompressedEntityRecord` to handle unknown or changed components:

- **Unknown component type** (hash not registered in current binary): log a warning, skip `dataSize` bytes, continue. The entity is loaded without that component.
- **Known component, larger data than expected**: read up to the registered component's size, skip remaining bytes. This handles a new field being added at the end of a component struct.
- **Known component, smaller data than expected**: fill missing fields with defaults. This handles a field being removed (via deprecation — never re-order or remove struct fields; deprecate by zeroing).

These rules also apply to `PlayerProfile` and `MatchRecord` deserialization.

### 8.3 Game Extension Data Versioning

The `gameExtensionData` blob in `PlayerProfile` and the `ExtensionBlock` in checkpoints are entirely game-managed. The engine writes and reads them as opaque bytes. The game must implement its own versioning within these blobs.

Recommended pattern: prefix the blob with a 2-byte game-extension version, then skip unknown fields on older versions:

```cpp
std::vector<uint8_t> serializeGameExtension() const
{
    engine::BitStreamWriter w;
    w.writeU16(kGameExtensionVersion);   // e.g., 3
    w.writeU32(someNewField_);           // added in version 3
    w.writeU16(anotherField_);           // added in version 2
    w.writeU8(originalField_);           // version 1
    return w.toBytes();
}

void deserializeGameExtension(std::span<const uint8_t> data)
{
    engine::BitStreamReader r(data);
    uint16_t ver = r.readU16();
    if (ver >= 3) someNewField_  = r.readU32();
    if (ver >= 2) anotherField_  = r.readU16();
    if (ver >= 1) originalField_ = r.readU8();
}
```

### 8.4 Breaking Changes

If a breaking schema change cannot be handled by the above rules, bump `gameVersion` and perform an explicit migration in the load path:

```cpp
// In onInit, after loading checkpoint:
engine::CheckpointData cp = ctx.save.loadCheckpoint(path);
if (cp.header.gameVersion < kCurrentGameVersion) {
    migrateCheckpoint(cp, cp.header.gameVersion, kCurrentGameVersion);
}
```

The engine does not provide an automatic migration framework — game-side migration functions are the recommended approach.

---

## 9. C++ Save System API

The full `SaveSystem` API available through `GameContext::save`:

### 9.1 Player Profiles

```cpp
// Load a player profile by account ID.
// Returns a default-constructed profile if the file does not exist.
PlayerProfile SaveSystem::loadProfile(uint64_t accountId);

// Write a player profile to disk (atomic write).
// Blocks until I/O completes; call from game tick, not render tick.
void SaveSystem::saveProfile(uint64_t accountId, const PlayerProfile& profile);

// Returns true if a profile file exists for this account ID.
bool SaveSystem::profileExists(uint64_t accountId) const;

// Delete a profile file (e.g., account deletion).
void SaveSystem::deleteProfile(uint64_t accountId);

// Flush all in-progress async profile writes (call from IGame::onShutdown).
void SaveSystem::flushAll();
```

### 9.2 Match Records

```cpp
// Write a completed match record. Assigns a unique filename automatically.
// Returns the filename (without directory) for logging/reference.
std::string SaveSystem::writeMatchRecord(const MatchRecord& record);

// Load a match record by its match ID (for display in post-game lobby, etc.).
// Returns nullopt if the file does not exist or is corrupt.
std::optional<MatchRecord> SaveSystem::loadMatchRecord(uint64_t matchId);

// Enumerate recent match records (returns up to maxCount, newest first).
std::vector<uint64_t> SaveSystem::listMatchRecords(uint32_t maxCount = 50);
```

### 9.3 Checkpoints

```cpp
// Write a checkpoint of the current world state.
// Serialization is synchronous on caller's thread; I/O is async on background thread.
// Safe to call from a game tick system.
void SaveSystem::writeCheckpoint();

// Write a checkpoint to a specific path (overrides rotation policy).
void SaveSystem::writeCheckpointTo(std::string_view path);

// Load a checkpoint from a file path. Throws on corruption / version mismatch.
CheckpointData SaveSystem::loadCheckpoint(std::string_view path);

// Apply a loaded checkpoint to the ECS world (restores entity state).
// Must be called before the first game tick; world should contain only the base scene.
void SaveSystem::applyCheckpointToWorld(const CheckpointData& cp, engine::World& world);

// Returns true if the engine was launched with a --resume flag pointing to a valid checkpoint.
bool SaveSystem::hasCheckpointToResume() const;

// Returns the path of the checkpoint to resume from (valid only if hasCheckpointToResume()).
std::string_view SaveSystem::resumeCheckpointPath() const;

// Delete checkpoint files older than the rotation policy.
// Called automatically by writeCheckpoint(); exposed for manual invocation.
void SaveSystem::pruneOldCheckpoints();

// Register per-entity component serializers for game-defined components.
// Must be called before the first writeCheckpoint().
template<typename T>
void SaveSystem::registerCheckpointComponent();
```

### 9.4 Game Version

```cpp
// Set the game-defined version packed into save file headers.
// Call before any save operations, typically in IGame::onInit.
void SaveSystem::setGameVersion(uint32_t version);

// Retrieve the game version embedded in a file header without fully loading it.
uint32_t SaveSystem::peekGameVersion(std::string_view path);
```

### 9.5 Diagnostics

```cpp
// Returns the total size on disk of all persistence files (profiles + records + checkpoints).
uint64_t SaveSystem::getTotalDiskUsageBytes() const;

// Returns the path of the most recently written checkpoint, or empty if none.
std::string SaveSystem::latestCheckpointPath() const;
```

---

## 10. Integration Patterns and Best Practices

### 10.1 Registering Game Components for Checkpointing

Only components explicitly registered with `SaveSystem::registerCheckpointComponent<T>()` are serialized in checkpoints. Register game components in `IGame::onInit` before loading any checkpoint:

```cpp
void MyGame::onInit(engine::GameContext& ctx)
{
    // Register all game components for checkpointing
    ctx.save.registerCheckpointComponent<HealthComponent>();
    ctx.save.registerCheckpointComponent<WeaponComponent>();
    ctx.save.registerCheckpointComponent<InventoryComponent>();

    ctx.save.setGameVersion(kMyGameVersion);

    // Now safe to resume from checkpoint
    if (ctx.save.hasCheckpointToResume()) {
        resumeFromCheckpoint(ctx);
    } else {
        startFresh(ctx);
    }
}
```

### 10.2 Profile Write Timing

Avoid writing profiles inside game-tick hot paths. Profile writes are I/O-bound and can spike tick time. Preferred write points:

- On player disconnect
- On match end (batch-write all profiles together)
- On `IGame::onShutdown()`

For match stats, accumulate into an in-memory `MatchStatsAccumulator` per player each tick, then commit to profiles in a single batch at match end.

### 10.3 Atomic Writes and Crash Safety

`SaveSystem::saveProfile()` uses an atomic write protocol:

1. Serialize to a memory buffer.
2. Write to `<path>.tmp`.
3. `fsync` the temporary file.
4. Rename `.tmp` → target path (atomic on NTFS via `MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`).

If the process crashes between steps 2 and 4, the `.tmp` file is left on disk. On the next server startup, `SaveSystem` scans for and deletes orphaned `.tmp` files before processing any load requests.

### 10.4 Multi-Server Environments

If your deployment runs multiple server instances sharing a common network filesystem (e.g., NFS), the atomic write protocol is not sufficient — simultaneous writes from two servers for the same player can race. In this scenario:

- Use file-level advisory locks (`LockFileEx` / `flock`) around load-modify-save sequences.
- Or, better: ensure player account IDs are sharded to specific server instances so at most one server owns each profile at a time.
- Or: implement a thin profile service (HTTP API) and have the engine's save system use a provided `IProfileBackend` interface instead of local files.

The engine ships with the local-file backend only. The `IProfileBackend` extension point is available for game-side implementation.

### 10.5 Testing Persistence

For automated testing, `SaveSystem` accepts a root override path, allowing tests to write to a temp directory:

```cpp
// In test setup:
ctx.save.setRootPath(engine::getTempDirectory() / "save_test");

// Test body: exercise save/load
ctx.save.saveProfile(12345, testProfile);
auto loaded = ctx.save.loadProfile(12345);
assert(loaded.displayName == testProfile.displayName);

// Cleanup:
std::filesystem::remove_all(ctx.save.rootPath());
```

---

*Document maintained by the engine team. File issues against the `engine-phase6` milestone.*
