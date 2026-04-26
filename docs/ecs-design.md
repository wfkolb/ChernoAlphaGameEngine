# Entity Component System (ECS) Design

Status: Approved (Phase 1)
Owner: Team Leader
Implementation: task #19 (Phase 3), depends on task #17 (math).

This document defines the ECS architecture for the engine. It is the contract that the implementer of `core/ecs` and all systems consuming it must follow.

---

## 1. Goals

- **Cache-friendly iteration.** A typical frame iterates 5–50k entities across a handful of component types; the inner loop must be tight.
- **Stable entity identity** across frames. Network code (entity sync, snapshots) and the editor both reference entities by ID across frames.
- **Predictable system update order.** Determinism matters for server-authoritative networking; "whatever order the scheduler picks" is not acceptable in v1.
- **No template-heavy public surface.** Component registration must work without dragging the entire ECS implementation into every translation unit.
- **Editable from the editor.** The editor (Tools Lead) must be able to enumerate entities and components and mutate them through a reflection layer.

## 2. High-Level Choice: Archetype-Based Storage

**Decision: archetype-based ECS.**

### Comparison

| Approach | Iteration speed | Add/remove component cost | Memory | Verdict |
|---|---|---|---|---|
| **Archetype** (group entities by component set, store components in parallel arrays per archetype) | Excellent — fully linear over a single archetype's component arrays. | Moderate — moving an entity between archetypes copies its components. | Tight; no per-entity overhead beyond a small handle table. | **Chosen.** |
| Sparse-set (one dense array per component type, sparse index from entity to slot) | Good — but iterating multi-component queries requires intersecting sparse sets. | Cheap — independent per component. | Slightly looser; one sparse array per component type. | Rejected. Network snapshot code wants to iterate "all entities with Transform AND NetReplicated" hot, and archetypes win there. |
| Object/COM-style (entity = vtable of components) | Poor (pointer chasing). | Cheap. | Loose. | Rejected. |
| Bitset-of-types lookup, AoS storage | Mediocre on both axes. | n/a. | Loose. | Rejected. |

The archetype model is justified because:
- Networking's snapshot pass iterates `(Transform, NetReplicated, [optional]Velocity)` every tick; archetype storage gives a perfect linear walk.
- Rendering's culling pass iterates `(Transform, Renderable)`; same story.
- Component churn (add/remove) is rare on hot entities; it happens at spawn/despawn, which is bounded.

## 3. Entity Identity

```cpp
struct Entity {
    uint32_t index;        // slot in the entity table (recycled on destroy)
    uint32_t generation;   // bumped each time the slot is freed
};
static_assert(sizeof(Entity) == 8);
constexpr Entity kInvalidEntity{0xFFFF'FFFFu, 0u};
```

- **64-bit total** (index + generation), 32 bits each.
- A handle is valid iff `world.IsAlive(entity)` returns true; this checks `slot.generation == entity.generation`.
- The 32-bit generation is enough that wrap is not a practical concern within a session (a 1 kHz spawn loop takes ~50 days to wrap a single slot).
- `Entity` is trivially copyable, fits a register, and is safe to stuff into network packets and ImGui IDs.
- The first valid `Entity` in a fresh world has `index = 0, generation = 1`. Generation `0` is reserved for "never used", so `kInvalidEntity` (max index, gen 0) does not collide with real entities.

### Wire format

For network replication: serialize as `varint(index)` + `varint(generation)`. The connection's snapshot baseline maintains a known-set of entity handles, and snapshot deltas reference them by index alone with the generation implied by the baseline.

## 4. Components

### Registration

Components are registered at startup, before any entity is created:

```cpp
world.RegisterComponent<Transform>("Transform");
world.RegisterComponent<Velocity>("Velocity");
```

- Each registered type gets a `ComponentTypeId` (uint16_t, dense, 0..N-1).
- Component count is capped at **256** per world in v1. Bitsets are 256-bit fixed (`std::bitset<256>` or 4× `uint64_t`).
- Each registration captures: name (for editor + serializer), size, alignment, move/copy/destroy function pointers, and optional reflection metadata.
- Re-registration with the same name is allowed (no-op); re-registration with a different type or size at the same name is a hard error.

### Storage requirements

A registered component type must satisfy:
- Trivially relocatable (we move components when an entity changes archetype).
- Default-constructible **or** explicitly added with an initializer at `AddComponent` call sites.
- ≤ 256 bytes is the recommended ceiling (anything larger should hold a `Handle<>` to data in another allocator). Not enforced.
- No internal pointers to itself (defeats relocation).

### Built-in components (registered by core)

- `Transform` (position/rotation/scale, 64 bytes aligned).
- `Hierarchy` (parent + sibling links).
- `Name` (debug, optional, fixed-size SSO string).

All other components are registered by their owning module: `Renderable`, `Camera`, `Light`, etc., by rendering; `NetReplicated`, `NetOwner` by networking; etc.

## 5. Archetype Storage

An **archetype** is the set of entities that share the exact same component-type set.

```cpp
struct Archetype {
    Bitset<256> componentMask;
    std::vector<ComponentTypeId> componentTypes;       // sorted, mirrors mask
    std::vector<ComponentColumn> columns;              // one per type
    std::vector<Entity> entities;                      // row -> entity
};

struct ComponentColumn {
    void* data;                  // contiguous component storage
    size_t elementSize;
    size_t elementAlign;
    size_t capacity;             // in elements
    size_t count;                // == archetype.entities.size()
};
```

- All columns in an archetype have the same length (number of entities in it).
- Columns grow geometrically (1.5×) when capacity is hit. Growth is the only place an archetype reallocates; iteration never invalidates pointers within a single frame because we forbid component add/remove during iteration (see §7).
- Empty archetypes are kept around for at most 60 frames before being garbage-collected.

### Entity → archetype mapping

```cpp
struct EntitySlot {
    uint32_t generation;
    uint16_t archetypeId;     // index into world's archetype list
    uint32_t row;              // index within the archetype's entities array
};
```

The world owns a `std::vector<EntitySlot>` indexed by `Entity::index`. Deleted slots form a free-list threaded through the `archetypeId` field (with sentinel `0xFFFF`).

### Archetype graph

To make `AddComponent<T>` and `RemoveComponent<T>` fast, each archetype caches its neighbors:

```cpp
std::array<ArchetypeId, 256> addEdges;     // result of adding component i
std::array<ArchetypeId, 256> removeEdges;  // result of removing component i
```

Edges populate lazily on first traversal. `AddComponent` is then: follow the edge (or create the new archetype), copy the entity's row from old archetype to new, append the new component, free the old row.

## 6. Queries and Views

```cpp
auto view = world.View<Transform, Velocity>();
for (auto [entity, transform, velocity] : view) {
    transform.position += velocity.value * dt;
}
```

- `View<Ts...>` matches every archetype whose `componentMask` is a superset of `mask<Ts...>`.
- Iteration is per-archetype: outer loop walks matched archetypes, inner loop is a tight pointer-walk over the columns.
- Excluded components: `world.View<Transform>().Exclude<Frozen>()`.
- Optional components: `world.View<Transform>().Optional<Sprite>()` yields `Sprite*` (nullable) per row.
- Views are cheap to create (a vector of matching archetype pointers, cached by component-mask key on the world).

### Cache invalidation

The view cache is invalidated when:
- A new archetype is created (rare; bounded by `2^|registered components|` but in practice tens at most).
- An archetype is garbage-collected.

It is **not** invalidated by entity create/destroy or by component add/remove on existing archetypes.

## 7. Mutation Rules During Iteration

Inside a `View` loop:

- **Allowed:** mutate component fields of the entity being iterated.
- **Allowed:** queue commands via `world.CommandBuffer()` for deferred apply.
- **Forbidden:** `AddComponent`, `RemoveComponent`, `CreateEntity`, `DestroyEntity` directly. These would invalidate the very columns we're walking.

The CommandBuffer (a per-frame, per-thread linear allocator-backed queue) is flushed at the end of each system's tick. `app` calls `world.FlushCommandBuffer()` between systems and asserts it is empty after the last system in a frame.

## 8. Systems and Update Order

A **system** is just a free function (or callable) registered with the world:

```cpp
world.AddSystem("Physics", SystemPhase::FixedUpdate, &PhysicsTick);
world.AddSystem("Cull", SystemPhase::Render, &RenderCullSystem);
```

### Phases (executed in this fixed order each frame)

1. `Input` — drain raw input from the event bus into input components.
2. `FixedUpdate` — runs zero or more times per frame at a fixed 60 Hz step. Hosts physics, networked simulation.
3. `Update` — once per frame, variable dt. Animation, gameplay logic.
4. `LateUpdate` — once per frame. Camera follow, transform finalization.
5. `Render` — once per frame. Culling, draw submission.
6. `PostRender` — once per frame. Debug overlays, profiler readout.

Within a phase, systems execute in **registration order**. The team-leader-owned `app` registers all systems in a single function so the order is auditable in one place.

### Determinism

- Systems are single-threaded in v1.
- Floating-point order is preserved by registration order.
- Networked clients and the server register systems in identical order. This is enforced by a startup hash check (the system list's order hash is logged on init).

## 9. Memory and Allocation

- The ECS uses two arenas owned by `core/memory`:
  - **`worldArena`**: long-lived. Backs archetype column storage and the entity slot table.
  - **`frameArena`**: reset every frame. Backs CommandBuffer entries and view scratch space.
- No `new`/`delete` in the inner ECS code; it routes through allocator handles.
- Archetype column storage uses aligned allocations matching the maximum component alignment in that archetype.

## 10. Reflection (minimal)

Each registered component carries:

- `name` (string view, stable for process lifetime)
- `serialize(void* component, Serializer&)` (function pointer, optional — required for `NetReplicated`-tagged components)
- `inspect(void* component, EditorContext&)` (function pointer, optional — used by editor)

Reflection registration is opt-in per component. Networking refuses to mark a component `NetReplicated` if `serialize` is missing.

## 11. Thread Safety

- The ECS world is **single-writer** in v1. All mutation runs on the simulation thread.
- Reads from other threads (e.g., the render-submission thread reading transforms) are allowed only through a snapshot taken at the end of `LateUpdate`. This is a copy of the transforms-of-interest into an immutable buffer; it is **not** a pointer into archetype storage.
- This restriction is documented and enforced by debug-build assertions on `World` methods (thread-id check).

## 12. Error Handling

- Unknown component type at lookup → assertion in Debug, `nullptr` return in Release with logged error.
- Stale entity handle at `Get<T>` → assertion in Debug, `nullptr` return in Release.
- Archetype overflow (>2^16 archetypes) → fatal. v1 does not expect to come close.

## 13. What Is NOT in v1

- **Multi-threaded systems / job graph.** Phase 3 stays single-threaded.
- **Hierarchical change events.** A `Hierarchy` component exists, but moving a parent does NOT auto-mark children dirty. Systems that care must walk the hierarchy themselves.
- **Component versioning / change detection.** Possibly added for editor undo and for skipping unchanged-network-component diffs in v2.
- **Persistent ECS scenes on disk.** The asset importer can produce prefabs in v2; v1 builds entities programmatically in the bootstrap.

## 14. Phase 3 Implementation Checklist (preview of task #19)

Implementer must deliver:

- [ ] `Entity`, `EntitySlot`, `World`, `Archetype`, `ComponentColumn` types.
- [ ] Component registration API + 256-component cap with assertion.
- [ ] `AddComponent`, `RemoveComponent`, `Get`, `Has`, `IsAlive`.
- [ ] `View<Ts...>` with `.Exclude<>` and `.Optional<>`, archetype-graph cache.
- [ ] `CommandBuffer` and `FlushCommandBuffer`.
- [ ] System registration + phase-ordered execution.
- [ ] Built-in components: `Transform`, `Hierarchy`, `Name`.
- [ ] Unit tests in `tests/core/ecs/` covering: stable handles across archetype moves, view iteration correctness, command buffer ordering, generation reuse.
