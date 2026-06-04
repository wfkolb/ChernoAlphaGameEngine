# 06 — Physics & Movement: `PhysicsWorld` and `CharacterController`

Each active scene owns exactly one `PhysicsWorld`. It runs at a **fixed 64 Hz timestep** synchronized with the game tick. Game systems set desired velocities before the step; physics resolves collisions and writes results back to `Transform` components.

## Tick integration

```
PrePhysics systems    — set CharacterController.desiredVelocity, apply forces
PhysicsWorld::step()  — integrate, resolve contacts, write Transform
PostPhysics systems   — read final Transform (positions are authoritative here)
```

The `GameLoop` calls `step(fixedDt)` between the `PrePhysics` and `PostPhysics` tick groups automatically. Never call `step()` from game code.

## Collision shapes

```cpp
// src/physics/public/physics/ColliderShape.h
namespace engine::physics {

struct ColliderShape {
    // Factory helpers — use these instead of constructing directly
    static ColliderShape box(engine::core::math::Vec3 halfExtents);
    static ColliderShape sphere(float radius);
    static ColliderShape capsule(float radius, float halfHeight);
    static ColliderShape convexHull(engine::core::AssetHandle hull);
    static ColliderShape triangleMesh(engine::core::AssetHandle mesh);

    // localOffset positions the shape relative to the entity's Transform origin
    engine::core::math::Vec3 localOffset = {};
};

} // namespace engine::physics
```

Shape selection guide:

| Shape | Best for | Notes |
|-------|----------|-------|
| `box` | Walls, crates, floors | Fastest narrow phase (SAT) |
| `sphere` | Grenades, sensors | Cheapest broad phase |
| `capsule` | Player, AI agents | Smooth slope/stair sliding |
| `convexHull` | Irregular props | Asset must be pre-cooked; max ~64 verts |
| `triangleMesh` | Level world geometry | **Static only** — never on a dynamic RigidBody |

## `RigidBody` component

```cpp
// src/physics/public/physics/RigidBody.h
namespace engine::physics {

struct RigidBody {
    // kComponentId = 6
    enum BodyType : uint8_t { Static, Kinematic, Dynamic };
    BodyType bodyType  = Dynamic;

    float mass         = 1.f;        // kg; ignored for Static/Kinematic
    float friction     = 0.5f;
    float restitution  = 0.2f;
    float linearDamping  = 0.05f;
    float angularDamping = 0.05f;

    engine::core::math::Vec3 linearVelocity  = {};
    engine::core::math::Vec3 angularVelocity = {};
    engine::core::math::Vec3 force           = {};   // applied each step, then cleared

    uint32_t freezeAxes = 0;  // RB_FREEZE_ROT_X | RB_FREEZE_ROT_Y | RB_FREEZE_ROT_Z
};

} // namespace engine::physics
```

### Body type usage

- **Static** — level geometry, walls. Zero runtime cost. Position changes require a rebuild.
- **Kinematic** — moving platforms, doors. Set `linearVelocity` or `Transform.position` directly.
- **Dynamic** — physics-simulated objects (crates, projectiles, ragdolls).

## `CharacterController` component

The `CharacterController` implements move-and-slide: each tick it projects the desired velocity along surfaces and resolves penetration without tunnelling.

```cpp
// src/physics/public/physics/CharacterController.h
namespace engine::physics {

struct CharacterController {
    // kComponentId = 7
    float capsuleRadius   = 0.35f;    // metres
    float capsuleHalfHeight = 0.85f;  // half the cylinder height (feet to mid-torso)
    float stepUpHeight    = 0.35f;    // maximum stair step the controller climbs
    float maxSlopeAngle   = 46.f;     // degrees; steeper = treated as wall

    // Written by game code each PrePhysics tick
    engine::core::math::Vec3 desiredVelocity = {};
    bool                     wantJump        = false;

    // Read by game code after PostPhysics
    bool  isGrounded = false;
    float coyoteTime = 0.f;     // seconds since last grounded (for coyote-time jumps)
    int   jumpBuffer = 0;       // ticks remaining in jump buffer window
};

} // namespace engine::physics
```

### Movement system pattern

```cpp
// Registered in TickGroup::PrePhysics
void playerMovementSystem(engine::core::ecs::World& world, float dt) {
    world.query<engine::core::input::InputReceiverComponent,
                engine::physics::CharacterController,
                engine::core::math::Transform>(
        [dt](engine::core::ecs::EntityId,
             const engine::core::input::InputReceiverComponent& rcv,
             engine::physics::CharacterController& cc,
             const engine::core::math::Transform& xform)
        {
            auto& input = engine::core::input::InputSystem::get();
            float fwd    = input.queryAnalog1D("MoveForward", rcv.playerId);
            float strafe = input.queryAnalog1D("MoveStrafe",  rcv.playerId);
            bool  sprint = input.queryHeld("Sprint", rcv.playerId);
            bool  jump   = input.queryJustPressed("Jump", rcv.playerId);

            // Build wish direction in world space from camera yaw
            engine::core::math::Vec3 wishDir =
                rotateByYaw(xform.rotation, { strafe, 0.f, fwd });

            float speed = sprint ? kSprintSpeed : kWalkSpeed;
            cc.desiredVelocity = wishDir.normalized() * speed;

            if (jump)
                cc.jumpBuffer = kJumpBufferTicks;  // handled inside PhysicsWorld::step
        });
}

// Registered in TickGroup::PostPhysics — sync physics results to the camera
void cameraFollowSystem(engine::core::ecs::World& world, float dt) {
    world.query<engine::core::math::Transform,
                engine::physics::CharacterController>(
        [](engine::core::ecs::EntityId,
           engine::core::math::Transform& xform,
           const engine::physics::CharacterController& cc)
        {
            // Camera eye is at capsule top (2 * halfHeight from feet)
            xform.position.y += cc.capsuleHalfHeight * 2.f;
        });
}
```

## Physics queries

All query functions are called **outside** `step()` — typically in `PostPhysics` or `GameFixed` systems.

### Raycast

```cpp
// src/physics/public/physics/PhysicsWorld.h
struct RaycastHit {
    engine::core::ecs::EntityId entity = engine::core::ecs::NULL_ENTITY;
    engine::core::math::Vec3    point;
    engine::core::math::Vec3    normal;
    float                       distance = 0.f;
};

// Returns true if the ray hit anything.
bool PhysicsWorld::raycast(
    engine::core::math::Vec3 origin,
    engine::core::math::Vec3 direction,
    float maxDistance,
    RaycastHit& outHit,
    uint32_t layerMask = 0xFFFF);
```

```cpp
// Check line-of-sight from player eyes to a point
engine::physics::RaycastHit hit;
if (ctx.scene->physics.raycast(eyePos, toTarget.normalized(),
                               toTarget.length(), hit)) {
    bool blocked = (hit.entity != targetEid);
}
```

### Capsule sweep

Used for melee hit detection or physics-aware movement previews:

```cpp
bool PhysicsWorld::sweepCapsule(
    float radius, float halfHeight,
    engine::core::math::Vec3 start,
    engine::core::math::Vec3 end,
    RaycastHit& outHit,
    uint32_t layerMask = 0xFFFF);
```

### Overlap sphere

Returns all entities whose colliders overlap a sphere:

```cpp
void PhysicsWorld::overlapSphere(
    engine::core::math::Vec3 center,
    float radius,
    std::vector<engine::core::ecs::EntityId>& outEntities,
    uint32_t layerMask = 0xFFFF);
```

## Collision layers

The physics system supports 16 named layers. Define them in `config/physics_layers.toml` and reference them by name:

```toml
# config/physics_layers.toml
[layers]
Default    = 0
Player     = 1
Projectile = 2
Trigger    = 3
Static     = 4

[matrix]
# rows/columns are layer indices; 1 = collide, 0 = ignore
# Projectile ignores Player (hitscan uses lag-compensated raycast instead)
Projectile.Player = 0
Trigger.Trigger   = 0
```

Assign a layer in your collider setup:

```cpp
collider.layerIndex = engine::physics::PhysicsLayerIndex::fromName("Player");
```

## Physics materials

Friction and restitution can be overridden per-surface via `PhysicsMaterialTable`:

```toml
# config/physics_materials.toml
[[materials]]
name        = "Metal"
friction    = 0.3
restitution = 0.1

[[materials]]
name        = "Rubber"
friction    = 0.9
restitution = 0.6
```

```cpp
collider.materialName = "Metal";
```

When two surfaces meet, the engine blends their material properties using the configured combine mode (default: average).

## Trigger volumes

A collider with `trigger = true` does not generate contact forces. Instead, the engine fires callbacks on overlap enter/exit:

```cpp
engine::physics::Collider triggerCollider;
triggerCollider.shape   = engine::physics::ColliderShape::box({ 2.f, 1.f, 2.f });
triggerCollider.trigger = true;

ctx.world.addComponent<engine::physics::Collider>(bombSiteEid, triggerCollider);

// Subscribe to overlap events (on the EventBus)
ctx.eventBus.subscribe<engine::physics::TriggerEnterEvent>(
    [&](const engine::physics::TriggerEnterEvent& e) {
        if (e.trigger == bombSiteEid && hasBomb(e.other))
            beginBombPlant(ctx, e.other);
    });
```

## Next

[07 — Combat](07_combat.md): `DamageSystem`, hitscan validation, and the `EventBus`.
