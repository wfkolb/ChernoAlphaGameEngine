# Gameplay Loop: Server-Authoritative FPS Architecture

**Phase 6 — Engine Version 0.6.x**
**Modules:** `engine::core`, `engine::networking`
**Audience:** Game developers implementing game modes, simulation systems, and multiplayer gameplay on the engine.

---

## Table of Contents

1. [High-Level Loop Structure](#1-high-level-loop-structure)
2. [Server Game Loop](#2-server-game-loop)
3. [Client Game Loop](#3-client-game-loop)
4. [WorldState Snapshot](#4-worldstate-snapshot)
5. [Client-Side Prediction and Reconciliation](#5-client-side-prediction-and-reconciliation)
6. [Lag Compensation for Hit Detection](#6-lag-compensation-for-hit-detection)
7. [Gameplay Hooks: IGameMode](#7-gameplay-hooks-igamemode)
8. [Win/Loss Conditions](#8-winloss-conditions)
9. [Game-Mode State Replication](#9-game-mode-state-replication)

---

## 1. High-Level Loop Structure

The engine runs three interleaved periodic loops on both server and client. Their rates and purposes differ:

| Loop | Rate | Owner | Purpose |
|---|---|---|---|
| **Game tick** | 64 Hz (15.625 ms) | Both | Deterministic simulation: input, physics, game logic |
| **Render tick** | Variable (vsync / uncapped) | Client only | Interpolation, animation, draw submission |
| **Network snapshot** | ~20 Hz (every 3 game ticks) | Both | Snapshot broadcast (server→client), input upload (client→server) |

```
Time ──────────────────────────────────────────────────────────────►

Server: [G][G][G][N][G][G][G][N][G][G][G][N] ...   (G = game tick, N = network emit)
Client: [G][G][G][N][G][G][G][N][G][G][G][N] ...   (game ticks run same cadence)
Client: [R]  [R]  [R]  [R]  [R]  [R]  [R]  [R]    (render ticks: uncapped)
```

The game tick is driven by a fixed accumulator in `Application::run()`. If the machine is slow and multiple game ticks are owed, they are processed back-to-back up to a maximum of `max_fixed_ticks_per_frame` (default 4). If this cap is hit, the simulation slows down rather than spiraling.

The render tick always renders the most recently completed game tick's state, interpolated forward in time by the fractional accumulator remainder to avoid stutter.

---

## 2. Server Game Loop

The server runs the full simulation and is the only authoritative source of game state.

### 2.1 Per-Tick Sequence

```
BEGIN FIXED TICK (tick N, duration = 1/64 s)
│
├── NetworkSystem::receive()
│     For each client C:
│       - Process all pending InputFrame packets from C's input buffer
│       - Apply most recent InputFrame to C's NetworkedInputComponent
│       - Record arrival tick for lag compensation
│
├── [TickGroup::PrePhysics systems]
│     - PlayerMovementSystem: consume NetworkedInputComponent, set CharacterController wish velocities
│     - WeaponSystem: process fire/reload inputs, begin hitscan traces via lag-compensated raycasts
│     - GameModeSystem: tick round timer, check spawn queue
│
├── PhysicsSystem::step(fixedDt)
│     - Integrate velocities, resolve collisions (character controllers, rigidbodies)
│
├── [TickGroup::PostPhysics systems]
│     - DamageSystem: resolve pending damage events produced by WeaponSystem
│     - HealthSystem: apply damage, check death conditions
│     - RespawnSystem: handle pending respawns
│
├── IGame::onGameTick()     (game-side hook; runs after all built-in systems)
│
├── GameModeSystem::evaluate()
│     - Check win/loss conditions, fire OnRoundEnd / OnMatchEnd if met
│
└── NetworkSystem::send()   (every 3rd tick, i.e., ~20 Hz)
      - Build WorldState snapshot from current ECS state
      - Delta-compress against last ACKed snapshot per client
      - Send via reliable-ordered channel (snapshot reliability) or unreliable + priority
```

### 2.2 Pseudocode

```cpp
// engine-internal (simplified); game developers do not implement this
void ServerLoop::runTick(uint32_t tick)
{
    // 1. Receive
    network.receive();
    for (auto& [pid, client] : clients) {
        if (client.hasPendingInput()) {
            InputFrame frame = client.dequeueInput();
            world.setComponent<NetworkedInputComponent>(client.playerEntity, { frame });
        }
    }

    // 2. Pre-physics systems
    scheduler.runGroup(TickGroup::PrePhysics, fixedDt);

    // 3. Physics
    physics.step(fixedDt);

    // 4. Post-physics systems
    scheduler.runGroup(TickGroup::PostPhysics, fixedDt);

    // 5. Game-side hook
    game.onGameTick(ctx, fixedDt);

    // 6. Game mode evaluate
    gameMode.evaluate(world, tick);

    // 7. Snapshot (every kSnapshotInterval ticks)
    if (tick % kSnapshotInterval == 0) {
        WorldStateSnapshot snap = snapshotBuilder.build(world, gameMode, tick);
        for (auto& [pid, client] : clients) {
            network.sendSnapshot(client, snap);
        }
    }
}
```

### 2.3 Input Buffer Behavior

The server maintains a short input buffer per client (default: 3 ticks deep) to absorb jitter. If a client's input frame for tick `N` arrives before the server processes tick `N`, it is buffered. If it arrives late, the server reuses the previous frame. The buffer depth is tunable per client based on measured RTT.

---

## 3. Client Game Loop

The client runs a local simulation in parallel with the server, using client-side prediction. The client's simulation is not authoritative but allows zero-latency response to local player input.

### 3.1 Per-Tick Sequence

```
BEGIN FIXED TICK (tick N)
│
├── InputSystem::collectFrame()
│     - Read hardware input, assemble InputFrame for tick N
│     - Store InputFrame in prediction history buffer
│
├── NetworkSystem::receive()
│     - Process inbound server snapshot packets
│     - If snapshot for tick M arrives: schedule reconciliation from tick M
│
├── [TickGroup::PrePhysics systems]  ← runs on predicted (local) state
│     - PlayerMovementSystem: apply local InputFrame
│     - WeaponSystem: apply local input (visual/audio only for non-authoritative outcomes)
│
├── PhysicsSystem::step(fixedDt)    ← local prediction physics
│
├── [TickGroup::PostPhysics systems]
│
├── IGame::onGameTick()
│
├── NetworkSystem::send()           (every tick — input is sent at full 64 Hz)
│     - Send InputFrame for tick N to server
│
└── [Reconciliation, if a server snapshot arrived this tick]
      - See Section 5
```

### 3.2 Remote Entity Interpolation

Remote entities (other players, physics objects not owned by the local player) are not predicted. They are interpolated between the two most recent server snapshots at render time.

```
Server snapshots received:    [S_18]        [S_21]        [S_24]
Client render time:               ──────────►
                                       ↑ interpolating between S_18 and S_21
```

The interpolation buffer keeps the last `kInterpolationBufferDepth` (default: 6) snapshots. Entities not present in a snapshot are extrapolated or hidden based on game logic.

### 3.3 Render Tick Pseudocode

```cpp
void ClientRenderTick::run(float renderDt, float tickAlpha)
{
    // tickAlpha: fractional [0,1] distance into the current tick window
    //            used to interpolate between last and next predicted positions

    scheduler.runGroup(TickGroup::Render, renderDt);

    // Interpolate remote entities for rendering
    for (auto& [eid, remoteState] : interpolationBuffer) {
        TransformComponent t = lerp(remoteState.prev, remoteState.next, tickAlpha);
        renderWorld.setTransform(eid, t);
    }

    // Build and submit frame
    renderer.buildFrameGraph(renderWorld, camera);
    renderer.submit();
}
```

---

## 4. WorldState Snapshot

A `WorldStateSnapshot` is a compact serialized representation of all networked simulation state at a given tick. It is the only data the server sends to clients.

### 4.1 Snapshot Contents

```cpp
struct WorldStateSnapshot
{
    uint32_t  tick;           // server tick this was taken after
    float     serverTime;     // server wall-clock seconds (for lag compensation reference)

    // --- Entity data ---
    struct EntityRecord
    {
        engine::NetworkEntityId netId;

        // Transform
        engine::Vec3  position;     // 12 bytes
        engine::Quat  orientation;  // 16 bytes (or compressed to 6 bytes with smallest-3)
        engine::Vec3  velocity;     // 12 bytes

        // Game state (game-registered fields)
        float         health;
        float         armor;
        uint8_t       weaponSlot;
        uint8_t       ammoInMag;
        uint8_t       ammoReserve;
        uint8_t       teamIndex;
        uint8_t       playerState;   // enum: alive, dead, spectating, respawning
        bool          isADS;
        bool          isCrouching;
    };
    std::vector<EntityRecord> entities;

    // --- Game-mode state (see Section 9) ---
    GameModeStateBlob gameModeState;

    // --- Destroyed entities since last snapshot ---
    std::vector<engine::NetworkEntityId> destroyedEntities;

    // --- Events (one-shot, not persistent) ---
    struct SnapshotEvent
    {
        uint8_t type;           // PlayerDied, WeaponFired, BombPlanted, etc.
        uint32_t entityId;
        float    originX, originY, originZ;
    };
    std::vector<SnapshotEvent> events;
};
```

### 4.2 Serialization

Snapshots are serialized to a compact binary format using the engine's built-in `BitStream` writer. Key optimizations:

- **Smallest-3 quaternion compression:** 6 bytes per rotation (drop largest component, transmit sign + remaining 3 as 16-bit fixed point).
- **Delta compression per client:** only fields that changed since the client's last ACKed snapshot are transmitted. Unchanged fields are omitted and reconstructed from the client's known baseline.
- **Positional quantization:** world-space positions are quantized to 0.5 mm precision using 21-bit fixed-point in a bounded world volume.
- **Entity relevancy culling:** entities outside a client's relevancy sphere (default 200 m) are not included in that client's snapshot.

Typical snapshot packet size for a 10-player game: 300–600 bytes uncompressed, 150–300 bytes after delta compression.

### 4.3 Snapshot Acknowledgement

Clients send ACK packets for each snapshot they successfully receive, identified by tick number. The server uses the ACK tick to compute the delta baseline for the next snapshot sent to that client. If no ACK is received for `kSnapshotAckTimeoutMs` (default 500 ms), the server sends a full (non-delta) snapshot.

---

## 5. Client-Side Prediction and Reconciliation

### 5.1 Prediction Model

The client runs the simulation at the same 64 Hz tick rate as the server. For the local player entity, the client applies input immediately and advances the simulation without waiting for server confirmation. For all other entities, the client interpolates between server snapshots.

The local player's predicted state diverges from the server's authoritative state by the round-trip latency. The reconciliation pass corrects this divergence.

### 5.2 Reconciliation Algorithm

```
Client receives server snapshot for tick M:

1. If predictedState[M] ≈ serverState[M] within threshold:
   → No action needed; continue predicting forward.

2. If divergence exceeds threshold:
   a. Overwrite local entity state with serverState[M].
   b. Replay all InputFrames from M+1 to currentTick:
      for (uint32_t t = M+1; t <= currentTick; ++t) {
          applyInput(inputHistory[t]);
          physicsSystem.step(fixedDt);
          // run relevant simulation systems
      }
   c. The replayed state is now the corrected prediction.
   d. To avoid jarring snaps: blend corrected position toward
      current rendered position over kReconcileBlendFrames frames.
```

The engine provides `PredictionSystem` which manages steps 2a–2c automatically. The game does not implement the replay loop directly; it ensures its simulation systems are **deterministic and stateless** — given the same inputs from a given starting state, they always produce the same output.

### 5.3 Determinism Requirements

For prediction/replay to work correctly:

- Systems in `TickGroup::GameFixed` must not read wall-clock time or generate random numbers using unseeded RNGs. Use `engine::Deterministic::random(seed)` which is tick-seeded.
- Systems must not read non-ECS state that can change between a live tick and a replay tick. Physics queries use the deterministic physics state snapshotted at the rollback point.
- Do not issue side effects (audio, particle spawn) during replay ticks. The engine passes a `TickContext::isReplay` flag for this purpose:

```cpp
void WeaponSystem::update(engine::World& world, float dt, engine::TickContext& tctx)
{
    world.query<WeaponComponent, NetworkedInputComponent>(
        [&](engine::EntityId eid, WeaponComponent& w, const NetworkedInputComponent& inp) {
            if (inp.current.digitalJustPressed & kFireActionBit) {
                fireWeapon(world, eid, w);
                if (!tctx.isReplay) {
                    audio.playOneShot("weapon_fire", xform.position);  // audio only on live tick
                }
            }
        });
}
```

### 5.4 Divergence Threshold

The reconciliation threshold is configurable:

```toml
[network.prediction]
position_error_threshold_cm = 2.0    # trigger reconcile if > 2 cm off
angle_error_threshold_deg   = 0.5    # trigger reconcile if > 0.5 degrees off
blend_frames                = 6      # render-tick frames to blend over after correction
```

Setting this too tight causes excessive reconciliations (jitter). Setting it too loose causes visible snapping when corrections do occur. The defaults are tuned for 50 ms RTT; adjust for your expected player base latency range.

---

## 6. Lag Compensation for Hit Detection

Hitscan weapons (bullets that resolve instantly, without a projectile entity) must account for the fact that the client fires at a visual state that is `RTT/2` ms older than the current server state. Without lag compensation, the server would reject hits that the client clearly saw land.

### 6.1 Algorithm

```
Client fires at render tick T_c:
  - Client's view shows entity E at position P_client
  - Client sends InputFrame containing Fire action + look direction
  - Frame arrives at server at server tick T_s

Server processes Fire at tick T_s:
  1. Determine the client's estimated view time:
       viewTime = T_s - (clientRTT / 2)
  2. Query the position history of entity E at time viewTime:
       P_rewound = lagCompHistory.queryAt(E, viewTime)
  3. Perform the hitscan raycast against P_rewound (rewound hitbox)
  4. If ray intersects rewound hitbox → register hit
  5. Restore entity positions (rewind is read-only; no state mutation)
```

### 6.2 Position History Buffer

The server maintains a per-entity circular buffer of historical `TransformComponent` snapshots, keyed by server tick. The default depth is 128 ticks (2 seconds at 64 Hz), sufficient to cover up to 500 ms RTT with headroom.

```cpp
// Engine-internal; game code queries via LagCompSystem
struct PositionHistoryBuffer
{
    static constexpr size_t kDepth = 128;

    struct Entry {
        uint32_t      tick;
        engine::Vec3  position;
        engine::Quat  orientation;
        engine::Bounds hitbox;           // AABB or capsule params
    };

    Entry entries[kDepth];
    size_t head = 0;

    Entry queryAt(float serverTime) const;   // linear interpolate between two ticks
};
```

### 6.3 Integrating Lag Compensation in WeaponSystem

The engine exposes `LagCompQuery` for game systems:

```cpp
void WeaponSystem::handleFireEvent(engine::World& world,
                                   engine::EntityId shooterEid,
                                   engine::NetworkedInputComponent& input)
{
    engine::TransformComponent& shooterXform =
        world.getComponent<engine::TransformComponent>(shooterEid);

    engine::Vec3 rayOrigin = shooterXform.position + kMuzzleOffset;
    engine::Vec3 rayDir    = input.current.lookDirection;

    float clientViewTime = input.current.clientTimestamp - (input.clientRtt * 0.5f);

    engine::LagCompQuery query{};
    query.ray         = { rayOrigin, rayDir, kMaxBulletRange };
    query.viewTime    = clientViewTime;
    query.shooterTeam = world.getComponent<TeamComponent>(shooterEid).index;
    query.ignoreFlags = engine::LagCompFlags::IgnoreSameTeam
                      | engine::LagCompFlags::IgnoreShooter;

    engine::LagCompResult result = engine::LagCompSystem::get().query(world, query);

    if (result.hit) {
        engine::DamageEvent dmg{};
        dmg.targetEntity = result.hitEntity;
        dmg.amount       = computeDamage(result.hitZone, weapon.damage);
        dmg.instigator   = shooterEid;
        world.emitEvent<engine::DamageEvent>(dmg);
    }
}
```

### 6.4 Anti-Cheat Bounds

Lag compensation is capped: the server will not rewind further back than `kMaxLagCompWindowMs` (default 200 ms). A client claiming a view timestamp older than this cap has its hit rejected. This prevents adversarial lag abuse from clients spoofing extreme latencies.

Additionally, the server validates that the ray origin is within a plausible distance of the shooter's last-known server position before performing any rewind. Rays originating far outside the player's expected location are discarded.

---

## 7. Gameplay Hooks: IGameMode

`IGameMode` is the primary extension point for round/match logic. Implement one concrete subclass per game mode.

```cpp
// engine/core/IGameMode.h
namespace engine {

class IGameMode
{
public:
    virtual ~IGameMode() = default;

    // Called once after the map is loaded and the game mode is installed.
    virtual void onRoundStart(GameContext& ctx, uint32_t roundNumber) {}

    // Called each fixed game tick while the round is live.
    virtual void onRoundTick(GameContext& ctx, float dt, uint32_t tick) {}

    // Called when a player is fully connected and ready to spawn.
    virtual void onPlayerJoin(GameContext& ctx, PlayerId pid) {}

    // Called when a player disconnects.
    virtual void onPlayerLeave(GameContext& ctx, PlayerId pid) {}

    // Called when a player entity is created and placed in the world.
    // Return the spawn transform to use (game mode controls spawn points).
    virtual engine::Transform onPlayerSpawn(GameContext& ctx, PlayerId pid) = 0;

    // Called when a player's health reaches zero.
    // The game mode decides whether to create a ragdoll, show death cam, etc.
    virtual void onPlayerDeath(GameContext& ctx, PlayerId victim, PlayerId killer) = 0;

    // Called at the end of a round (win condition met or timer expired).
    virtual void onRoundEnd(GameContext& ctx, RoundResult result) {}

    // Called at the end of the match (all rounds complete, or match timer).
    virtual void onMatchEnd(GameContext& ctx, MatchResult result) {}

    // Called each network tick to fill the GameModeStateBlob in the snapshot.
    // Return serialized bytes; max 256 bytes.
    virtual std::vector<uint8_t> serializeState() const = 0;

    // Called on the client when a snapshot arrives with a new GameModeStateBlob.
    virtual void deserializeState(std::span<const uint8_t> data) = 0;

    // Called to determine if a win/loss condition has been met this tick.
    // Return a non-null RoundResult to end the round.
    virtual std::optional<RoundResult> evaluateWinCondition(
        const GameContext& ctx) const = 0;
};

} // namespace engine
```

### 7.1 Example: Team Deathmatch

```cpp
class TeamDeathmatchMode : public engine::IGameMode
{
public:
    static constexpr int kKillsToWin = 30;
    static constexpr float kRoundTimeSec = 600.f; // 10 minutes

    void onRoundStart(engine::GameContext& ctx, uint32_t round) override
    {
        teamKills_[0] = teamKills_[1] = 0;
        roundTimeRemaining_ = kRoundTimeSec;
        roundNumber_ = round;
    }

    engine::Transform onPlayerSpawn(engine::GameContext& ctx,
                                     engine::PlayerId pid) override
    {
        uint8_t team = getPlayerTeam(pid);
        return spawnPoints_[team].nextAvailable();
    }

    void onPlayerDeath(engine::GameContext& ctx,
                       engine::PlayerId victim,
                       engine::PlayerId killer) override
    {
        if (killer != engine::kInvalidPlayerId) {
            uint8_t killerTeam = getPlayerTeam(killer);
            ++teamKills_[killerTeam];
        }
        scheduleRespawn(ctx, victim, kRespawnDelaySec);
    }

    void onRoundTick(engine::GameContext& ctx, float dt, uint32_t) override
    {
        roundTimeRemaining_ -= dt;
    }

    std::optional<engine::RoundResult> evaluateWinCondition(
        const engine::GameContext& ctx) const override
    {
        for (int t = 0; t < 2; ++t) {
            if (teamKills_[t] >= kKillsToWin) {
                return engine::RoundResult{ .winningTeam = (uint8_t)t };
            }
        }
        if (roundTimeRemaining_ <= 0.f) {
            uint8_t winner = (teamKills_[0] >= teamKills_[1]) ? 0 : 1;
            return engine::RoundResult{ .winningTeam = winner };
        }
        return std::nullopt;
    }

    std::vector<uint8_t> serializeState() const override; // see Section 9
    void deserializeState(std::span<const uint8_t> data) override;

private:
    int    teamKills_[2]{};
    float  roundTimeRemaining_ = 0.f;
    uint32_t roundNumber_      = 0;
};
```

### 7.2 Round Lifecycle

```
Server onInit()
  └─► GameMode::onRoundStart(ctx, round=1)
        └─► players connect → GameMode::onPlayerJoin() → GameMode::onPlayerSpawn()
              └─► [game ticks, network snapshots]
                    └─► GameMode::evaluateWinCondition() returns RoundResult
                          └─► GameMode::onRoundEnd(ctx, result)
                                └─► delay, then GameMode::onRoundStart(ctx, round=2)
                                      └─► [repeat ...]
                                            └─► GameMode::onMatchEnd(ctx, matchResult)
```

---

## 8. Win/Loss Conditions

Win conditions are evaluated by `IGameMode::evaluateWinCondition()` every game tick. When a non-null `RoundResult` is returned, the engine calls `onRoundEnd()` and freezes player movement for `kPostRoundFreezeSec` (default 3 s) before either starting the next round or calling `onMatchEnd()`.

### 8.1 RoundResult and MatchResult

```cpp
struct RoundResult
{
    uint8_t winningTeam     = 0xFF;    // 0xFF = draw
    std::string reason;                // "kill_limit", "time_expired", "bomb_exploded", etc.
    bool        isTechnical = false;   // true if ended due to server error / empty server
};

struct MatchResult
{
    uint8_t winningTeam    = 0xFF;
    int     roundsWon[2]   = {};
    int     totalKills[2]  = {};
    float   matchDuration  = 0.f;     // seconds
    std::string mapName;
    std::string modeName;
};
```

### 8.2 Plugging in Custom Conditions

Because `evaluateWinCondition()` is pure virtual, the engine never hardcodes any win condition logic. Examples of custom conditions:

- **Bomb defusal:** check if the bomb entity's `BombComponent.state == Exploded || BombComponent.state == Defused`.
- **VIP escort:** check if the VIP entity reached the extraction zone entity's bounds.
- **Last man standing:** check if only one player's `HealthComponent.current > 0`.

All game-mode state needed for evaluation should be stored as member variables of the `IGameMode` subclass or as ECS components on special singleton entities.

---

## 9. Game-Mode State Replication

All state visible to clients must be included in the `WorldStateSnapshot`. The `GameModeStateBlob` field carries the game mode's custom state.

### 9.1 Serialization Contract

`serializeState()` is called on the server once per snapshot (every 3 game ticks). It must produce a deterministic, compact byte buffer. Maximum size is 256 bytes. If your game mode needs more space, store large data in the entity list using custom `NetworkedEntityComponent` extensions.

```cpp
std::vector<uint8_t> TeamDeathmatchMode::serializeState() const
{
    engine::BitStreamWriter writer;
    writer.writeU8(roundNumber_);
    writer.writeU16((uint16_t)teamKills_[0]);
    writer.writeU16((uint16_t)teamKills_[1]);
    writer.writeF32(roundTimeRemaining_);
    writer.writeU8(/* round phase: warmup/live/post */ phase_);
    return writer.toBytes();
}

void TeamDeathmatchMode::deserializeState(std::span<const uint8_t> data)
{
    engine::BitStreamReader reader(data);
    roundNumber_          = reader.readU8();
    teamKills_[0]         = reader.readU16();
    teamKills_[1]         = reader.readU16();
    roundTimeRemaining_   = reader.readF32();
    phase_                = static_cast<RoundPhase>(reader.readU8());
}
```

### 9.2 Client-Side Game-Mode State

On the client, `deserializeState()` is called whenever a snapshot arrives with a changed `GameModeStateBlob`. The client game mode instance is updated in the `Network` tick phase, before any `Render` tick systems run, so the HUD always reads current replicated state.

The client game mode is a mirror of the server's: it receives state but does not independently evaluate win conditions. If the client's game mode needs to fire local events (e.g., play a "round won" sound), it should compare old and new state in `deserializeState()` and emit engine events:

```cpp
void TeamDeathmatchMode::deserializeState(std::span<const uint8_t> data)
{
    int oldKills0 = teamKills_[0];
    // ... deserialize ...
    if (teamKills_[0] != oldKills0) {
        engine::EventBus::get().emit(KillCountChangedEvent{ .team = 0, .kills = teamKills_[0] });
    }
}
```

### 9.3 Score and Timer in the HUD

The HUD system reads game mode state through a typed accessor:

```cpp
// In HUDSystem::update (TickGroup::Render)
const TeamDeathmatchMode* mode =
    ctx.network.getGameMode<TeamDeathmatchMode>();

if (mode) {
    hudData.teamAKills         = mode->getTeamKills(0);
    hudData.teamBKills         = mode->getTeamKills(1);
    hudData.roundTimeRemaining = mode->getRoundTimeRemaining();
}
```

Do not store a raw pointer to the game mode object across ticks — the engine may replace it if the mode changes (e.g., at match end/restart). Always re-query via `getGameMode<T>()` each tick.

---

*Document maintained by the engine team. File issues against the `engine-phase6` milestone.*
