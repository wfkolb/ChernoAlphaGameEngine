# Physics Engine Design

**Phase 6 — Physics & Scene Lead Reference**
**Engine:** Windows DX12 / C++20 | `engine::core` / `engine::physics` modules

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Collision Shapes](#2-collision-shapes)
3. [Rigid Bodies](#3-rigid-bodies)
4. [Character Controller](#4-character-controller)
5. [Collision Detection Pipeline](#5-collision-detection-pipeline)
6. [Triggers vs Colliders](#6-triggers-vs-colliders)
7. [Raycasting and Shape Sweeps](#7-raycasting-and-shape-sweeps)
8. [Physics Layers and Filtering](#8-physics-layers-and-filtering)
9. [Physics Materials](#9-physics-materials)
10. [Integration with the Game Tick](#10-integration-with-the-game-tick)
11. [Determinism and Server-Authoritative Multiplayer](#11-determinism-and-server-authoritative-multiplayer)
12. [API Reference](#12-api-reference)

---

## 1. Architecture Overview

Each `Scene` owns exactly one `PhysicsWorld`. The `PhysicsWorld` is the single authoritative physics simulation for that scene and runs at a **fixed 64 Hz timestep** (`fixedDt = 1.0 / 64.0 = 0.015625 s`) that is synchronised with the engine's game tick.

```
Engine main loop (variable rate)
│
├── accumulate unspent time
├── while (accum >= fixedDt):
│       GameSystems::tick(fixedDt)     ← AI, input, game logic
│       PhysicsWorld::step(fixedDt)    ← physics integration + collision
│       accum -= fixedDt
│
└── render (interpolated state between physics ticks)
```

### Module Layout

```
engine/physics/
  PhysicsWorld.h/.cpp          — top-level simulation container
  RigidBodySolver.h/.cpp       — constraint + integration
  CharacterController.h/.cpp   — capsule mover
  BroadPhase.h/.cpp            — AABB grid for dynamic entities
  NarrowPhase.h/.cpp           — GJK/EPA, SAT implementations
  CollisionShapes.h/.cpp       — shape descriptors + support functions
  TriggerSystem.h/.cpp         — overlap tracking + event dispatch
  PhysicsMaterialTable.h/.cpp  — loads physics_materials.toml
  RaycastSystem.h/.cpp         — ray + shape sweep queries
```

### Threading Model

- `PhysicsWorld::step()` is called from the main game thread (Phase 6).
- Broad-phase pair generation is multi-threaded via the engine job system (up to 4 worker threads).
- Narrow-phase contact generation runs in parallel across pairs.
- Constraint solving and integration are single-threaded (can be parallelised in a future phase).
- Game callbacks (`OnCollision`, `TriggerCallback`) are dispatched on the main thread after `step()` returns.

---

## 2. Collision Shapes

Shapes are described by `ColliderShape`, a discriminated union. The shape data is stored inside the `Collider` component (small shapes) or in a referenced asset (trimesh, convex hull).

```cpp
namespace engine::physics {

enum class ShapeType : uint8_t {
    Box, Sphere, Capsule, ConvexHull, TriangleMesh
};

struct ColliderShape {
    ShapeType type;

    union {
        struct { Vec3 halfExtents; }    box;
        struct { float radius; }        sphere;
        struct { float radius;
                 float halfHeight; }    capsule;
        struct { AssetHandle hull; }    convex;
        struct { AssetHandle mesh; }    triMesh;
    };

    // Factory helpers
    static ColliderShape box(Vec3 halfExtents);
    static ColliderShape sphere(float radius);
    static ColliderShape capsule(float radius, float halfHeight);
    static ColliderShape convexHull(AssetHandle hull);
    static ColliderShape triangleMesh(AssetHandle mesh);
};

} // namespace engine::physics
```

### Shape Selection Guide

| Shape | Best Use | Notes |
|---|---|---|
| **Box** | Crates, walls, simple props, floors | Fastest narrow phase (SAT). Use by default for rectangular objects. |
| **Sphere** | Grenades, ball projectiles, proximity sensors | Cheapest broad phase test. |
| **Capsule** | Player, AI agents, NPCs | The standard humanoid shape. Smooth sliding on slopes and stairs. No sharp edges that snag on geometry. |
| **ConvexHull** | Irregular props (barrels, vehicles), weapons | Slower than Box/Sphere. Asset must be pre-cooked. Limit to ~64 verts. |
| **TriangleMesh** | Level world mesh, terrain | **Static only.** Never use on a dynamic rigid body. Pre-cooked `.phys` asset required. |

### Shape Offsets

A `Collider` component has an optional local offset (`Vec3 offset` and `Quat rotation`) to position the shape relative to the entity's `Transform` origin. This is needed for e.g. a capsule character whose `Transform` origin is at the feet but the capsule center is at mid-body.

```cpp
// Player capsule: feet at origin, capsule centered 0.85m above feet
collider.shape         = ColliderShape::capsule(0.35f, 0.85f);
collider.localOffset   = Vec3{0.f, 0.85f, 0.f};  // center offset from feet
```

---

## 3. Rigid Bodies

The `RigidBody` component marks an entity as physics-simulated. Entities without a `RigidBody` that have a `Collider` are treated as static geometry.

### Body Types

| Type | Flag | Behaviour |
|---|---|---|
| **Static** | `RB_FLAG_STATIC` | Immovable. Baked into BVH. Zero runtime cost. |
| **Kinematic** | `RB_FLAG_KINEMATIC` | Moved by setting `linearVelocity` or `Transform.position` directly. Does not respond to forces. Pushes dynamic bodies. Used for moving platforms, doors. |
| **Dynamic** | `RB_FLAG_DYNAMIC` | Fully simulated. Responds to forces, gravity, collisions. |

### RigidBody Fields

```cpp
struct RigidBody {
    // Classification
    uint32_t flags            = RB_FLAG_DYNAMIC;

    // Inertia
    float    mass             = 1.0f;       // kg; ignored for static/kinematic
    Vec3     inertiaTensor    = Vec3::one(); // diagonal; auto-computed if zero

    // Damping (applied every tick to bleed energy)
    float    linearDamping    = 0.05f;      // fraction of velocity removed per second
    float    angularDamping   = 0.05f;

    // Surface response
    float    friction         = 0.5f;       // overridden by PhysicsMaterial if set
    float    restitution      = 0.2f;       // bounciness; 0 = inelastic, 1 = perfectly elastic

    // State (written by physics system each tick)
    Vec3     linearVelocity   = Vec3::zero();
    Vec3     angularVelocity  = Vec3::zero();
    Vec3     force            = Vec3::zero();  // accumulated this tick; cleared after integration
    Vec3     torque           = Vec3::zero();

    // Constraints
    uint8_t  freezeLinearAxes  = 0;  // bitmask: bit0=X, bit1=Y, bit2=Z
    uint8_t  freezeAngularAxes = 0;  // bitmask: bit0=X, bit1=Y, bit2=Z
};
```

### Applying Forces and Impulses

```cpp
// Apply a continuous force (e.g. rocket thrust) — accumulated until next step()
PhysicsWorld::get(scene).addForce(entityId, thrustDir * 500.f);

// Apply an instant impulse (e.g. explosion knockback) — applied immediately
PhysicsWorld::get(scene).addImpulse(entityId, blastDir * 1200.f);

// Set velocity directly (kinematic, or character controller)
PhysicsWorld::get(scene).setVelocity(entityId, Vec3{0.f, jumpSpeed, 0.f});
```

---

## 4. Character Controller

The character controller is a specialised kinematic capsule mover designed for responsive FPS player movement. It bypasses the rigid body solver for the player character itself and instead uses a **move-and-slide** algorithm.

### Design Principles

- The game code sets a **desired velocity** each tick; the controller resolves collisions and adjusts the final displacement.
- No forces or impulses are applied to the character controller externally (it is not a `RigidBody`).
- The controller does push dynamic rigid bodies it collides with (imparts an impulse proportional to its mass and velocity).

### Component Fields

```cpp
struct CharacterController {
    // Shape
    float    capsuleRadius      = 0.35f;    // metres
    float    capsuleHalfHeight  = 0.85f;    // metres from center to top/bottom spheres

    // Movement config
    float    stepUpHeight       = 0.25f;    // max step the controller can climb
    float    maxSlopeAngle      = 46.0f;    // degrees; steeper = sliding, no movement

    // State (read by game code)
    bool     isGrounded         = false;
    bool     isStepping         = false;    // currently climbing a step
    Vec3     groundNormal       = Vec3::up();
    EntityId groundEntity       = NULL_ENTITY;

    // Input (written by game code each tick, consumed by PhysicsWorld::step())
    Vec3     desiredVelocity    = Vec3::zero();

    // Optional helpers (see below)
    float    coyoteTimeRemaining = 0.f;
    float    jumpBufferRemaining = 0.f;
};
```

### Moving the Character Each Tick

```cpp
// In your PlayerMoveSystem (runs before PhysicsWorld::step)
void PlayerMoveSystem::tick(float dt, Scene& scene) {
    scene.query<CharacterController, const InputReceiver>(
        [&](EntityId id, CharacterController& cc, const InputReceiver& input) {

            Vec3 moveDir = input.moveAxis.x * right + input.moveAxis.y * forward;
            float speed = input.isSprinting ? 7.0f : 5.0f;

            Vec3 wishVel = moveDir * speed;
            wishVel.y    = cc.desiredVelocity.y;   // preserve vertical (gravity / jump)

            // Apply gravity
            if (!cc.isGrounded) {
                wishVel.y -= 9.81f * dt;
            }

            // Jump
            if (input.jumpPressed && (cc.isGrounded || cc.coyoteTimeRemaining > 0.f)) {
                wishVel.y = 6.5f;   // initial jump velocity m/s
                cc.coyoteTimeRemaining = 0.f;
            }

            cc.desiredVelocity = wishVel;
        }
    );
}
```

`PhysicsWorld::step()` then resolves the controller's movement against static and dynamic geometry.

### Coyote Time

The controller tracks `coyoteTimeRemaining`: for a brief window after walking off a ledge, the player can still jump. This is a quality-of-life feature for FPS movement.

```
Default coyote time: 0.12 seconds
Configured in: CharacterControllerConfig.coyoteTime (PhysicsWorld setting)
```

### Jump Buffering

If `jumpPressed` is received slightly before the character lands (up to 0.10s), the jump is buffered and fires immediately upon grounding. Tracked by `jumpBufferRemaining`.

### Move-and-Slide Algorithm

```
1. Cast capsule downward by stepUpHeight to detect ground / step.
2. Decompose desiredVelocity into tangential and normal components relative to ground.
3. Project tangential velocity onto the slope plane.
4. If slope angle > maxSlopeAngle: zero tangential velocity (slide only).
5. Sweep capsule by projected velocity * dt.
6. On collision with wall: project remaining velocity along wall plane; repeat up to 3 iterations.
7. Update Transform.position and isGrounded flag.
```

---

## 5. Collision Detection Pipeline

### Broad Phase

The broad phase quickly identifies **candidate pairs** of colliders that might be overlapping, without performing exact intersection tests.

- **Static geometry:** queried via the BVH (built once at scene load).
- **Dynamic entities:** tracked in a uniform grid (4m × 4m cells). Each tick, entities update their grid cells. Pairs sharing a cell are candidates.

Output: a list of `ColliderPair{EntityId a, EntityId b}` that are handed to the narrow phase.

### Narrow Phase

Each candidate pair undergoes an exact intersection test:

| Shape Pair | Algorithm |
|---|---|
| Box vs Box | SAT (Separating Axis Theorem) — 15 axes tested |
| Sphere vs Sphere | Distance check |
| Box vs Sphere | Closest point on box to sphere center |
| Capsule vs Capsule | Segment-to-segment closest distance |
| Capsule vs Box | GJK (Gilbert–Johnson–Keerthi) |
| ConvexHull vs Convex | GJK + EPA (Expanding Polytope Algorithm) for penetration depth |
| Any vs TriangleMesh | AABB tree traversal on mesh + per-triangle SAT/GJK |

GJK requires **support functions** for each shape — the physics module registers these at startup. Adding a new convex shape requires implementing its support function and registering it.

### Contact Manifold Generation

When narrow phase confirms an intersection, it generates a **contact manifold**: up to 4 contact points, each with a world-space position, normal, and penetration depth.

```cpp
struct ContactPoint {
    Vec3  position;       // world space
    Vec3  normal;         // from B towards A
    float penetration;    // metres of overlap
    float impulse;        // resolved impulse magnitude (post-solver)
};

struct ContactManifold {
    EntityId        entityA;
    EntityId        entityB;
    ContactPoint    points[4];
    uint8_t         pointCount;
};
```

### Constraint Solver

The solver processes all manifolds using an **iterative impulse-based approach** (10 iterations per tick). For each contact:

1. Compute relative velocity at the contact point.
2. Compute the impulse needed to resolve penetration and satisfy restitution.
3. Clamp to non-negative (no pulling).
4. Accumulate warm-starting data for the next tick.

Friction is modelled as two tangential constraints per contact point (Coulomb friction cone).

---

## 6. Triggers vs Colliders

A `Collider` with `isTrigger = true` participates in broad phase and narrow phase **overlap detection** but generates **no contact manifold** and applies **no impulse**. It fires `TriggerCallback` events instead.

| Property | Regular Collider | Trigger Collider |
|---|---|---|
| Blocks movement | Yes | No |
| Generates contact | Yes | No |
| Fires OnEnter/OnStay/OnExit | No | Yes |
| Appears in raycast hits | Yes (unless filtered) | Configurable (`QueryFilter.includeTriggers`) |
| CPU cost | Higher (solver) | Lower (overlap test only) |

### Layer Matrix Interaction

Triggers only fire for entity-vs-trigger pairs where the layer collision matrix says the two layers can interact. Example: a `Trigger` layer overlaps with `Player` layer but not with `Projectile` layer.

---

## 7. Raycasting and Shape Sweeps

These are the primary way game code queries the physics world outside of collision callbacks. They test against **both** the static BVH and the dynamic broad-phase grid.

### RaycastHit

```cpp
struct RaycastHit {
    Vec3        point;          // world-space impact point
    Vec3        normal;         // outward surface normal at impact
    float       distance;       // metres along the ray
    EntityId    entityId;       // entity whose collider was hit
    uint32_t    physMatIndex;   // index in PhysicsMaterialTable for surface effects
    bool        isTrigger;      // true if the hit collider is a trigger
};
```

### API

```cpp
PhysicsWorld& pw = scene->physicsWorld();

// Single raycast — returns the closest hit
RaycastHit hit;
bool didHit = pw.raycast(
    Ray{ origin, direction, maxDistance },
    hit,
    QueryFilter{}.layers(PhysicsLayer::StaticWorld | PhysicsLayer::Player)
                 .exclude(shooterEntityId)
                 .includeTriggers(false)
);

// All hits along a ray (sorted by distance)
SmallVector<RaycastHit, 8> allHits;
int count = pw.raycastAll(Ray{ origin, dir, 200.f }, allHits, filter);

// Capsule sweep — used for character move-and-slide and AI vision cones
SweepResult sweep;
bool blocked = pw.sweepCapsule(
    origin, direction, sweepDistance,
    capsuleRadius, capsuleHalfHeight,
    sweep, filter
);

struct SweepResult {
    RaycastHit hit;
    float      hitFraction;    // 0..1 fraction of sweep that succeeded before impact
};

// Sphere overlap — explosion radius, proximity checks
std::array<EntityId, 64> overlapping;
int n = pw.overlapSphere(center, radius, overlapping, filter);
```

### Common FPS Queries

```cpp
// Hitscan weapon — single raycast from camera
bool weapon_hitscan(EntityId shooter, const Ray& ray, Scene& scene) {
    RaycastHit hit;
    QueryFilter f;
    f.layerMask    = PhysicsLayer::StaticWorld | PhysicsLayer::Player | PhysicsLayer::Dynamic;
    f.excludeEntity = shooter;

    if (scene->physicsWorld().raycast(ray, hit, f)) {
        apply_damage(hit.entityId, 25.f, scene);
        spawn_impact_decal(hit.point, hit.normal, hit.physMatIndex, scene);
        return true;
    }
    return false;
}

// Ground check (used before CharacterController to detect ledges)
bool is_on_ground(EntityId player, Scene& scene) {
    auto& t = scene->get<Transform>(player);
    RaycastHit hit;
    Ray down = { t.position + Vec3{0, 0.1f, 0}, Vec3{0, -1, 0}, 0.25f };
    return scene->physicsWorld().raycast(down, hit,
        QueryFilter{}.layers(PhysicsLayer::StaticWorld | PhysicsLayer::Dynamic)
    );
}

// Line-of-sight check for AI
bool has_line_of_sight(Vec3 from, Vec3 to, Scene& scene) {
    Vec3 dir = Vec3::normalize(to - from);
    float dist = Vec3::distance(from, to);
    RaycastHit hit;
    if (scene->physicsWorld().raycast(Ray{from, dir, dist}, hit,
            QueryFilter{}.layers(PhysicsLayer::StaticWorld))) {
        return false;  // something solid in the way
    }
    return true;
}
```

---

## 8. Physics Layers and Filtering

The engine provides **16 physics layers**. Each layer is a bit in a `uint16_t` layer mask. The **layer collision matrix** is a 16×16 symmetric boolean table that specifies which layers can interact.

### Default Layer Assignment

| Layer Index | Name | Typical Entities |
|---|---|---|
| 0 | `Default` | Generic dynamic objects |
| 1 | `StaticWorld` | Level mesh, static props |
| 2 | `Player` | Player CharacterController |
| 3 | `AI` | NPC CharacterControllers |
| 4 | `Projectile` | Bullets, grenades, rockets |
| 5 | `Trigger` | Trigger volumes |
| 6 | `Pickup` | Health packs, ammo, weapons on floor |
| 7 | `Dynamic` | Physics-simulated props |
| 8–13 | `UserDefined0–5` | Available to game code |
| 14 | `Ragdoll` | Post-death ragdoll bodies |
| 15 | `NoCollision` | Entities that should never collide (ghosts) |

### Default Collision Matrix (selected entries)

|  | Static | Player | AI | Projectile | Trigger | Pickup | Dynamic | Ragdoll |
|---|---|---|---|---|---|---|---|---|
| **Static** | No | Yes | Yes | Yes | No | No | Yes | Yes |
| **Player** | Yes | No | No | Yes | Yes | Yes | Yes | No |
| **Projectile** | Yes | Yes | Yes | No | No | No | Yes | Yes |
| **Trigger** | No | Yes | Yes | No | No | No | No | No |

### Configuring the Matrix

```toml
# physics_layers.toml
[collision_matrix]
# Format: "LayerA,LayerB" = true/false
"Default,Default"     = true
"Default,StaticWorld" = true
"Player,Player"       = false    # players don't collide with each other
"Player,Projectile"   = true
"AI,Player"           = false
"Projectile,Projectile" = false
```

### QueryFilter

```cpp
struct QueryFilter {
    uint16_t  layerMask         = 0xFFFF;     // layers to include
    EntityId  excludeEntity     = NULL_ENTITY;
    bool      includeTriggers   = false;
    bool      includeKinematic  = true;
    bool      includeDynamic    = true;
    bool      includeStatic     = true;
};
```

---

## 9. Physics Materials

Physics materials define the **friction** and **restitution** of surfaces. They are defined in `config/physics_materials.toml` and assigned to `Collider` components by index.

### physics_materials.toml

```toml
[[material]]
name        = "concrete"
friction    = 0.6
restitution = 0.1

[[material]]
name        = "metal"
friction    = 0.4
restitution = 0.3

[[material]]
name        = "ice"
friction    = 0.05
restitution = 0.1

[[material]]
name        = "wood"
friction    = 0.55
restitution = 0.25

[[material]]
name        = "rubber"
friction    = 0.9
restitution = 0.7

[[material]]
name        = "player_body"
friction    = 0.1   # low friction so player slides smoothly
restitution = 0.0
```

### Assigning to a Collider

```cpp
uint32_t matIdx = PhysicsMaterialTable::get().indexOf("concrete");
scene->get<Collider>(floorEntityId).physicsMaterialIndex = matIdx;
```

### Combining Rules

When two surfaces interact, their friction and restitution are combined:

```
effective_friction    = sqrt(matA.friction    * matB.friction)    // geometric mean
effective_restitution = max(matA.restitution, matB.restitution)   // maximum
```

These defaults can be overridden per-pair in `physics_materials.toml`:

```toml
[[pair_override]]
material_a  = "ice"
material_b  = "player_body"
friction    = 0.02   # nearly frictionless
```

### Surface FX Lookup

The `physMatIndex` in `RaycastHit` can be used to look up impact effects (decals, sounds):

```cpp
// In your WeaponSystem, after a raycast hit:
auto& mat = PhysicsMaterialTable::get().get(hit.physMatIndex);
AudioSystem::play(mat.impactSoundEvent, hit.point);
VFXSystem::spawn(mat.bulletDecalPrefab, hit.point, hit.normal);
```

Decal and sound references are stored alongside physics properties in `physics_materials.toml` as asset paths.

---

## 10. Integration with the Game Tick

### Fixed Timestep Loop

```cpp
// engine/core/Engine.cpp (simplified)
void Engine::run() {
    double accumulator = 0.0;
    double prevTime    = platform_time();

    while (running) {
        double now = platform_time();
        double frameTime = std::min(now - prevTime, 0.05);  // cap at 50ms
        prevTime = now;
        accumulator += frameTime;

        while (accumulator >= FIXED_DT) {
            inputSystem.collect();
            gameSystems.tick(FIXED_DT);           // user game code
            scene.physicsWorld().step(FIXED_DT);  // physics
            transformSystem.propagate();           // update matrices
            networkSystem.tick(FIXED_DT);          // snapshot generation
            accumulator -= FIXED_DT;
        }

        float alpha = static_cast<float>(accumulator / FIXED_DT);
        renderer.render(alpha);   // interpolated between physics frames
    }
}
```

### Interpolation for Rendering

Entity transforms are rendered at an **interpolated** position between the previous and current physics tick, scaled by `alpha`. This eliminates visual judder at display rates above 64 Hz.

```cpp
// TransformSystem stores two states for interpolation
struct InterpolatedTransform {
    Vec3 prevPosition;
    Quat prevRotation;
    Vec3 currPosition;
    Quat currRotation;
};

// Renderer uses:
Vec3 renderPos = Vec3::lerp(prev.prevPosition, prev.currPosition, alpha);
Quat renderRot = Quat::slerp(prev.prevRotation, prev.currRotation, alpha);
```

Only **dynamic** entities (those with a `RigidBody` or `CharacterController`) are interpolated. Static geometry is rendered at its baked position.

### PhysicsWorld::step() Internals

```
step(dt):
  1. Integrate forces → update velocities (semi-implicit Euler)
  2. Broad phase update (dynamic grid rebuild for moved entities)
  3. Narrow phase: generate contact manifolds
  4. Detect trigger overlaps; queue TriggerEvents
  5. Solve velocity constraints (N iterations)
  6. Integrate velocities → update positions
  7. Resolve character controller movements
  8. Copy position/rotation back to Transform components
  9. Dispatch collision callbacks (OnCollisionEnter/Stay/Exit)
  10. Dispatch trigger callbacks (OnEnter/Stay/Exit)
```

---

## 11. Determinism and Server-Authoritative Multiplayer

The engine targets **server-authoritative 64-tick multiplayer**. The physics system's role in this architecture is:

### Server-Side Authority

The authoritative physics simulation runs **only on the server**. Clients receive position/velocity snapshots at 64 Hz via the network system and interpolate between them for rendering.

### Client-Side Prediction

The **character controller** (player movement) runs locally on the owning client in parallel with the server. Client input is applied locally for immediate responsiveness. On receiving a server snapshot, the client:

1. Compares its predicted position with the server-authoritative position.
2. If the error exceeds a threshold (default 0.05m), snaps or smoothly corrects toward the server position.
3. Re-simulates all buffered inputs from the snapshot's tick forward (input re-simulation, max 64 frames buffered).

```cpp
// Simplified client reconciliation
void ClientPrediction::reconcile(const ServerSnapshot& snap) {
    float error = Vec3::distance(predictedPos, snap.position);
    if (error > CORRECTION_THRESHOLD) {
        // Hard snap for large errors (e.g. teleport, spawn)
        if (error > SNAP_THRESHOLD) {
            controller.position = snap.position;
        } else {
            // Smooth correct for small errors
            correctionVelocity = (snap.position - predictedPos) / CORRECTION_WINDOW;
        }
        // Re-simulate from snapshot tick
        resimulate(snap.tick, currentTick, bufferedInputs);
    }
}
```

### Determinism Constraints

Physics simulation on the server must be **deterministic** given the same inputs, to allow replay recording and cheat detection. Requirements:

- Use **32-bit float** arithmetic throughout (double is not consistent across SIMD paths).
- **Do not** depend on `std::unordered_map` iteration order for collision pair processing — pairs are sorted by `(entityA, entityB)` before processing.
- **Do not** use `rand()` or non-seeded RNG inside physics code.
- The physics tick rate (64 Hz) is fixed and never varies.
- Time-of-impact queries use conservative advancement, not ad-hoc `std::lerp`.

### Non-Deterministic Systems (Client Only)

Particle effects, audio, ragdolls after death, ambient physics props — these use the **client physics world** which has relaxed determinism requirements and can use the full dynamic simulation for visual fidelity.

---

## 12. API Reference

### PhysicsWorld

```cpp
namespace engine::physics {

class PhysicsWorld {
public:
    // Lifecycle
    void            step(float dt);
    void            reset();

    // Rigid bodies
    void            addRigidBody(EntityId id, const RigidBody& rb, const Collider& col);
    void            removeRigidBody(EntityId id);
    void            setVelocity(EntityId id, Vec3 linearVel);
    void            setAngularVelocity(EntityId id, Vec3 angVel);
    Vec3            getVelocity(EntityId id) const;
    void            addForce(EntityId id, Vec3 force);
    void            addImpulse(EntityId id, Vec3 impulse);
    void            addTorque(EntityId id, Vec3 torque);
    void            teleport(EntityId id, Vec3 pos, Quat rot);

    // Character controller
    void            addCharacterController(EntityId id, const CharacterController& cc);
    void            removeCharacterController(EntityId id);
    void            moveCharacter(EntityId id, Vec3 desiredVelocity, float dt);

    // Raycasting
    bool            raycast(const Ray& ray, RaycastHit& hit,
                            const QueryFilter& filter = {}) const;
    int             raycastAll(const Ray& ray,
                               std::span<RaycastHit> results,
                               const QueryFilter& filter = {}) const;

    // Shape queries
    bool            sweepCapsule(Vec3 origin, Vec3 dir, float dist,
                                 float radius, float halfHeight,
                                 SweepResult& result,
                                 const QueryFilter& filter = {}) const;
    int             overlapSphere(Vec3 center, float radius,
                                  std::span<EntityId> results,
                                  const QueryFilter& filter = {}) const;
    int             overlapBox(Vec3 center, Vec3 halfExtents, Quat rotation,
                               std::span<EntityId> results,
                               const QueryFilter& filter = {}) const;

    // Collision callbacks
    void            setCollisionCallback(
                        std::function<void(const ContactManifold&)> cb);

    // Global settings
    void            setGravity(Vec3 gravity);
    Vec3            getGravity() const;
    void            setLayerCollision(uint8_t layerA, uint8_t layerB, bool enabled);

    // Debug
    void            debugDraw(DebugRenderer& dbg) const;
};

} // namespace engine::physics
```

### Accessing PhysicsWorld from Game Code

```cpp
// From a system that has a Scene reference:
PhysicsWorld& pw = scene.physicsWorld();

// Shorthand helper (resolves active scene):
PhysicsWorld& pw = PhysicsWorld::active();
```
