# Task #68 — SpawnPoint System

**Phase 9 — core / app / editor — Version 0.9.x**
**Audience:** Gameplay Lead, Editor developer
**Depends on:** #51 (FpsArchetypes), #55 (IGameMode, GameLoop), #67 (viewport icons — can land after core wiring)
**Unblocks:** #69 (PIE physics+input), #74 (IGame bootstrap)

---

## 1. Goal

Level designers can place named spawn points in a scene. When a player joins, the server queries spawn point entities, asks the active `IGameMode` which one to use, and teleports the player there. During PIE, pressing Play automatically places the FpsCharacter at the nearest spawn point. Spawn points are an **engine-level concept** — the `GameLoop` drives the query; the `IGameMode` only picks between available candidates.

---

## 2. Current State

- `FpsArchetypes.cpp` registers a `SpawnPointEntity` archetype (`spawnWithTransform` — just a `Transform`), but it is not in the editor Spawn menu and has no dedicated component.
- `SceneGlobals` has a `spawnPoints` field (an array of positions/names) that is populated by nothing and read by nothing — a dead field in conflict with the ECS approach. **This field must be removed as part of this task** (see §7).
- `IGameMode` has `onPlayerSpawn()` but no `selectSpawnPoint()`.
- `GameLoop::onPlayerJoin()` does not look for spawn point entities.
- `PIEController::start()` does not seek out a spawn point before the first tick.

---

## 3. SpawnPointComponent

**File:** `src/core/public/core/components/SpawnPointComponent.h`

```cpp
namespace engine::core {

struct SpawnPointComponent {
    static constexpr ecs::ComponentTypeId kComponentId = 14;

    uint8_t  teamId;       // 0 = any team; 1–15 = team-specific
    uint8_t  priority;     // higher = preferred; used by default selectSpawnPoint impl
    float    exclusionRadius; // metres; players spawned here block this point for exclusionRadius seconds
    char     tag[32];      // optional designer label (e.g. "blue_base", "mid"); NUL-terminated
};

} // namespace engine::core
```

Register in `Engine::init()` after `PrefabInstance` (ID 13):
```cpp
world_.registerComponent<core::SpawnPointComponent>();
```

---

## 4. SpawnPointEntity Archetype Update

Update `FpsArchetypes.cpp` to add `SpawnPointComponent` when spawning a `SpawnPointEntity`:

```cpp
factory.registerArchetype("SpawnPointEntity",
    [](Entity e, const SpawnParams& p, World& w) {
        w.addComponent<engine::core::Transform>(e, {p.position, p.rotation});
        w.addComponent<engine::core::SpawnPointComponent>(e, {});
    });
```

---

## 5. IGameMode Interface Extension

**File:** `src/app/public/app/IGameMode.h`

Add one new virtual method:

```cpp
// Called by GameLoop when a player needs to be placed.
// availablePoints: entities with SpawnPointComponent that pass the teamId filter.
// Return kInvalidEntity to fall back to the engine default (first available).
virtual ecs::EntityId selectSpawnPoint(
    uint32_t             playerId,
    uint8_t              teamId,
    std::span<const ecs::EntityId> availablePoints)
{
    // Default: return the highest-priority point not in its exclusion window.
    if (availablePoints.empty()) return ecs::kInvalidEntity;
    return availablePoints[0];
}
```

The default implementation satisfies simple game modes. Team deathmatch modes override it to filter by `teamId`. Battle-royale modes can implement weighted-random selection.

---

## 6. GameLoop Spawn Wiring

**File:** `src/app/GameLoop.cpp`

Add a `spawnPlayerAtPoint()` helper called from `onPlayerJoin()` (and on respawn events if the game mode requests one):

```cpp
static void spawnPlayerAtSpawnPoint(
    ecs::EntityId playerEntity,
    uint8_t       teamId,
    IGameMode*    gameMode,
    ecs::World&   world)
{
    // Collect all valid spawn points.
    std::vector<ecs::EntityId> candidates;
    ecs::View<core::Transform, core::SpawnPointComponent> view(world);
    view.each([&](ecs::Entity e, core::Transform&, core::SpawnPointComponent& sp) {
        if (sp.teamId == 0 || sp.teamId == teamId)
            candidates.push_back(e);
    });

    ecs::EntityId chosen = ecs::kInvalidEntity;
    if (gameMode)
        chosen = gameMode->selectSpawnPoint(playerId, teamId, candidates);
    if (chosen == ecs::kInvalidEntity && !candidates.empty())
        chosen = candidates[0];
    if (chosen == ecs::kInvalidEntity) {
        LOG_WARN("GameLoop: no spawn point found for player {}; spawning at origin", playerId);
        return;
    }

    const auto* spawnTransform = world.tryGet<core::Transform>(chosen);
    if (!spawnTransform) return;

    if (auto* pt = world.tryGet<core::Transform>(playerEntity))
        *pt = *spawnTransform;
}
```

---

## 7. SceneGlobals Cleanup

`SceneGlobals::spawnPoints` (an array of hardcoded positions added in Phase 7 spec) conflicts with the ECS approach. Remove it:

- Remove field from `src/core/public/core/scene/SceneGlobals.h`
- Remove serialisation in `src/tools/SceneSerializer.cpp` (write zero-length section; add forward-compat skip on load for older scenes that have it)
- Remove any editor UI that exposes it (check `ScenePropertiesPanel`)

---

## 8. Editor Spawn Menu

**File:** `src/editor/panels/SceneHierarchyPanel.cpp`

Add `SpawnPointEntity` alongside `FpsCharacter`:

```cpp
if (ImGui::BeginMenu("Spawn")) {
    if (ImGui::MenuItem("FpsCharacter")) { /* existing */ }
    if (ImGui::MenuItem("SpawnPoint")) {
        SpawnParams params{};
        const Entity spawned = entityFactory_->spawn("SpawnPointEntity", params, world);
        if (spawned != kInvalidEntity) {
            selected = spawned;
            if (sceneDirty_) *sceneDirty_ = true;
        }
    }
    ImGui::EndMenu();
}
```

---

## 9. PIE Auto-Spawn

**File:** `src/editor/PIEController.cpp`

In `PIEController::start()`, after `captureSnapshot()`:

```cpp
// Find the FpsCharacter entity (has InputReceiverComponent).
// Find the nearest SpawnPointComponent entity.
// Set the FpsCharacter's Transform to match.
ecs::Entity player = ecs::kInvalidEntity;
ecs::Entity spawnPt = ecs::kInvalidEntity;

ecs::View<core::Transform, core::input::InputReceiverComponent> players(world);
players.each([&](ecs::Entity e, core::Transform&, core::input::InputReceiverComponent&) {
    if (player == ecs::kInvalidEntity) player = e;
});

ecs::View<core::Transform, core::SpawnPointComponent> spawns(world);
spawns.each([&](ecs::Entity e, core::Transform&, core::SpawnPointComponent&) {
    if (spawnPt == ecs::kInvalidEntity) spawnPt = e;
});

if (player != ecs::kInvalidEntity && spawnPt != ecs::kInvalidEntity) {
    const auto* st = scene_->world().tryGet<core::Transform>(spawnPt);
    if (auto* pt = scene_->world().tryGet<core::Transform>(player))
        *pt = *st;
}
```

---

## 10. Viewport Icon

Once #67 lands, `ViewportPanel` draws a green circle + `[S]` label at each `SpawnPointComponent` entity's world position using `worldToScreen()`. The icon is a fixed screen-space size (16 px radius) regardless of distance.

---

## 11. Tests

**File:** `tests/core/ecs/SpawnPointTests.cpp` (label: unit)

- `SpawnPointEntity` archetype spawns an entity with both `Transform` and `SpawnPointComponent`.
- `GameLoop` spawn logic: world with 3 spawn points (teamId 0, 1, 1); player with teamId 1; verify `selectSpawnPoint()` returns a point with teamId 0 or 1, not exclusively teamId 0.
- Default `selectSpawnPoint()` with empty list: returns `kInvalidEntity`.
- `PIEController::start()` with a scene containing one FpsCharacter + one SpawnPoint: after `start()`, player Transform matches spawn point Transform.

---

## 12. Open Questions

- **Q:** Should `exclusionRadius` be enforced in `GameLoop` (engine-level) or left entirely to the `IGameMode`? Recommendation: engine enforces basic deduplication (don't spawn two players at the exact same point); `IGameMode` handles strategic selection.
- **Q:** Should spawn points support a rotation (i.e., the player faces the direction the spawn point is facing)? Recommendation: yes — copy the full `Transform` (position + rotation) from the spawn point to the player on spawn.
