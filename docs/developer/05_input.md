# 05 — Input: Actions, Bindings, and `InputFrame`

The input system separates physical inputs (keys, mouse, gamepad) from logical actions ("Fire", "Jump"). Game code only queries named actions — bindings are data, not code.

## Registering actions

Register all actions once in `IGame::onInit`, before loading bindings:

```cpp
void ArenaGame::onInit(engine::app::GameContext& ctx) {
    engine::core::input::InputSystem& input = ctx.inputSystem;

    // Digital: held / justPressed / justReleased
    input.registerAction("Fire",        engine::core::input::ActionType::Digital);
    input.registerAction("ADS",         engine::core::input::ActionType::Digital);
    input.registerAction("Jump",        engine::core::input::ActionType::Digital);
    input.registerAction("Crouch",      engine::core::input::ActionType::Digital);
    input.registerAction("Sprint",      engine::core::input::ActionType::Digital);
    input.registerAction("Reload",      engine::core::input::ActionType::Digital);
    input.registerAction("Interact",    engine::core::input::ActionType::Digital);
    input.registerAction("WeaponSlot1", engine::core::input::ActionType::Digital);
    input.registerAction("WeaponSlot2", engine::core::input::ActionType::Digital);
    input.registerAction("WeaponSlot3", engine::core::input::ActionType::Digital);

    // Analog 1D: value in [-1, 1] (axes) or [0, 1] (triggers)
    input.registerAction("MoveForward", engine::core::input::ActionType::Analog1D);
    input.registerAction("MoveStrafe",  engine::core::input::ActionType::Analog1D);
    input.registerAction("LookYaw",     engine::core::input::ActionType::Analog1D);
    input.registerAction("LookPitch",   engine::core::input::ActionType::Analog1D);

    // Load default bindings from config/input.toml
    input.loadBindingsFromToml("config/input.toml");

    // Apply per-player rebinding overrides from the saved profile
    engine::tools::PlayerProfile profile = ctx.saveSystem.loadProfile(localAccountId_);
    if (!profile.bindingOverrides.empty())
        input.applyBindingOverrides(profile.bindingOverrides);
}
```

## Default `input.toml`

Place this file at `config/input.toml` in your game's content directory:

```toml
[[bindings]]
action = "Fire"
device = "mouse_button"
button = "left"

[[bindings]]
action = "ADS"
device = "mouse_button"
button = "right"

[[bindings]]
action = "Jump"
device = "keyboard"
key    = "space"

[[bindings]]
action = "Crouch"
device = "keyboard"
key    = "left_control"

[[bindings]]
action = "Sprint"
device = "keyboard"
key    = "left_shift"

[[bindings]]
action = "Reload"
device = "keyboard"
key    = "r"

[[bindings]]
action = "Interact"
device = "keyboard"
key    = "f"

[[bindings]]
action = "MoveForward"
device = "keyboard"
key    = "w"
scale  = 1.0

[[bindings]]
action = "MoveForward"
device = "keyboard"
key    = "s"
scale  = -1.0

[[bindings]]
action = "MoveStrafe"
device = "keyboard"
key    = "d"
scale  = 1.0

[[bindings]]
action = "MoveStrafe"
device = "keyboard"
key    = "a"
scale  = -1.0

[[bindings]]
action = "LookYaw"
device = "mouse_axis"
axis   = "x"
scale  = 1.0

[[bindings]]
action = "LookPitch"
device = "mouse_axis"
axis   = "y"
scale  = -1.0          # invert Y: mouse up = look up

[[bindings]]
action = "WeaponSlot1"
device = "keyboard"
key    = "1"

[[bindings]]
action = "WeaponSlot2"
device = "keyboard"
key    = "2"

[[bindings]]
action = "WeaponSlot3"
device = "keyboard"
key    = "3"

# --- Gamepad bindings ---

[[bindings]]
action       = "Fire"
device       = "gamepad_button"
button       = "right_trigger"
player_index = 0

[[bindings]]
action       = "Jump"
device       = "gamepad_button"
button       = "a"
player_index = 0

[[bindings]]
action       = "MoveForward"
device       = "gamepad_axis"
axis         = "left_stick_y"
deadzone     = 0.15
scale        = 1.0
player_index = 0

[[bindings]]
action       = "LookYaw"
device       = "gamepad_axis"
axis         = "right_stick_x"
deadzone     = 0.12
scale        = 1.0
player_index = 0
```

## Querying input in systems

Systems poll the `InputSystem` during the fixed game tick. Always query through the player's `InputReceiverComponent` to respect focus groups and priority:

```cpp
void PlayerMovementSystem(engine::core::ecs::World& world, float dt) {
    world.query<engine::core::input::InputReceiverComponent,
                engine::physics::CharacterController>(
        [dt](engine::core::ecs::EntityId,
             const engine::core::input::InputReceiverComponent& receiver,
             engine::physics::CharacterController& cc)
        {
            auto& input = engine::core::input::InputSystem::get();

            float fwd    = input.queryAnalog1D("MoveForward", receiver.playerId);
            float strafe = input.queryAnalog1D("MoveStrafe",  receiver.playerId);
            bool  jump   = input.queryJustPressed("Jump",     receiver.playerId);
            bool  sprint = input.queryHeld("Sprint",          receiver.playerId);

            cc.desiredVelocity = engine::core::math::Vec3(strafe, 0.f, fwd).normalized()
                                 * (sprint ? kSprintSpeed : kWalkSpeed);
            if (jump) cc.jumpBuffer = kJumpBufferTicks;
        });
}
```

## Look input — mouse vs. gamepad

Mouse deltas are **absolute per-tick counts**, not per-second rates. Do not multiply by `dt`:

```cpp
// Correct — mouse delta already accumulated over one fixed tick
float yawDelta   = input.queryDelta("LookYaw",   pid);
float pitchDelta = input.queryDelta("LookPitch", pid);
yaw   += yawDelta;
pitch += pitchDelta;

// Wrong — this double-integrates time
yaw += input.queryDelta("LookYaw", pid) * dt;
```

Gamepad sticks return a normalized rate `[-1, 1]`. Multiply by a speed constant and `dt`:

```cpp
float stickYaw = input.queryAnalog1D("LookYaw", pid) * kGamepadLookSpeedDegPerSec * dt;
```

Apply ADS sensitivity scaling after reading the delta — never by modifying `InputSystem` sensitivity, since that would corrupt the `InputFrame` values used for server-side lag compensation:

```cpp
float sensMult = weapon.isAimingDownSights ? settings.adsSensMult : 1.f;
yaw += input.queryDelta("LookYaw", pid) * sensMult;
```

## Focus groups

Multiple entities can consume input. The engine routes input based on `InputFocusGroup` priority:

```cpp
enum class InputFocusGroup { Gameplay, UI, Console, Cutscene };
```

Push a group onto the focus stack to steal input from gameplay (e.g. open a menu):

```cpp
ctx.inputSystem.pushFocusGroup(engine::core::input::InputFocusGroup::UI);
// ... user interacts with menu ...
ctx.inputSystem.popFocusGroup(engine::core::input::InputFocusGroup::UI);
```

## The networked `InputFrame`

At the end of each fixed tick the engine serializes current action state into an `InputFrame` and sends it to the server. Your code never constructs this manually — the engine does it. The structure is provided here for reference when writing server-side systems that read from `NetworkedInputComponent`:

```cpp
struct InputFrame {
    uint32_t tick;              // client fixed-tick counter
    uint64_t clientTimestamp;   // wall-clock ms (for lag compensation)
    uint64_t digitalHeld;       // bit per registered digital action
    uint64_t digitalJustPressed;
    float    lookYawDelta;      // raw mouse/stick delta this tick
    float    lookPitchDelta;    // pitch clamped ±89° before send
};
```

Server-side systems read from `NetworkedInputComponent`, not from `InputSystem`:

```cpp
// Server-side PlayerMovementSystem
world.query<engine::networking::NetworkedInputComponent,
            engine::physics::CharacterController>(
    [](engine::core::ecs::EntityId,
       const engine::networking::NetworkedInputComponent& inp,
       engine::physics::CharacterController& cc)
    {
        // Same logic as the client-side system — determinism is required for reconciliation
        float fwd    = inp.current.analogValue("MoveForward");
        float strafe = inp.current.analogValue("MoveStrafe");
        cc.desiredVelocity = engine::core::math::Vec3(strafe, 0.f, fwd).normalized()
                             * kWalkSpeed;
    });
```

## Runtime rebinding

```cpp
// In a key-binding settings screen:
std::string_view existing = input.getActionBoundTo(engine::core::input::Key::R);
if (!existing.empty()) {
    showConflictDialog(existing);
} else {
    input.rebindAction("Reload",
        engine::core::input::KeyboardBinding{ engine::core::input::Key::R });
}

// Persist the new bindings via the save system
profile.bindingOverrides = input.serializeBindingOverrides();
ctx.saveSystem.saveProfile(profile);
```

## Mouse raw input

Raw input bypasses the OS cursor acceleration curve. Enable it in `config/game.toml`:

```toml
[input.mouse]
raw_input = true   # use WM_INPUT, bypass OS acceleration
capture   = true   # confine cursor to window, hide it
```

## Next

[06 — Physics & Movement](06_physics_movement.md): `PhysicsWorld`, `CharacterController`, collision shapes, and raycasting.
