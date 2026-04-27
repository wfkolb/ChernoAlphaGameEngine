# Input System: Defining Actions and Input Receivers

**Phase 6 — Engine Version 0.6.x**
**Module:** `engine::core` — `engine/core/InputSystem.h`
**Audience:** Game developers implementing player input for an FPS title.

---

## Table of Contents

1. [Conceptual Overview](#1-conceptual-overview)
2. [InputAction: Named Logical Actions](#2-inputaction-named-logical-actions)
3. [InputBinding: Mapping Physical Inputs to Actions](#3-inputbinding-mapping-physical-inputs-to-actions)
4. [Defining Bindings in `input.toml`](#4-defining-bindings-in-inputtoml)
5. [Defining Bindings in C++](#5-defining-bindings-in-c)
6. [InputReceiver: ECS Component and Focus/Priority](#6-inputreceiver-ecs-component-and-focuspriority)
7. [Consuming Input in Systems](#7-consuming-input-in-systems)
8. [Networked Input: InputFrame and Server Submission](#8-networked-input-inputframe-and-server-submission)
9. [Client-Side Prediction with Input](#9-client-side-prediction-with-input)
10. [Runtime Rebinding and Player Settings](#10-runtime-rebinding-and-player-settings)
11. [FPS-Specific Patterns](#11-fps-specific-patterns)

---

## 1. Conceptual Overview

The input system is divided into two layers:

```
Physical layer                   Logical layer
─────────────────────────────    ────────────────────────────────
Keyboard key "Space"         →   Action "Jump"
Mouse button Left            →   Action "Fire"
Mouse axis X                 →   Action "LookYaw"
Gamepad Right trigger        →   Action "Fire"
Gamepad Left stick Y         →   Action "MoveForward"
```

Game code never reads `VK_SPACE` or raw mouse button state directly. It queries named logical actions. This decoupling means:
- Bindings are configurable without recompiling.
- The same action can be triggered by multiple physical inputs (keyboard/gamepad simultaneously supported).
- The networking layer can serialize an `InputFrame` that is action-centric, not device-centric, so clients and servers agree on the input vocabulary regardless of which device the player uses.

The input system is server-invisible on a dedicated server: the `InputSystem` subsystem is not instantiated at all on a headless dedicated server process. The server only ever sees `InputFrame` packets.

---

## 2. InputAction: Named Logical Actions

An `InputAction` has a name, a value type, and optional metadata. It is registered once, typically in `IGame::onInit`.

### 2.1 Action Types

| Type | C++ enum | Typical use |
|---|---|---|
| Digital | `ActionType::Digital` | Fire, Jump, Reload, Interact, Crouch, ADS, Sprint |
| Analog 1D | `ActionType::Analog1D` | MoveForward, MoveStrafe, LookPitch, LookYaw |
| Analog 2D | `ActionType::Analog2D` | Gamepad look stick (combined X+Y) |

### 2.2 Registering Actions

```cpp
// In MyGame::onInit or a dedicated InputSetup function
engine::InputSystem& input = ctx.input;

input.registerAction("Fire",        engine::ActionType::Digital);
input.registerAction("ADS",         engine::ActionType::Digital);
input.registerAction("Jump",        engine::ActionType::Digital);
input.registerAction("Crouch",      engine::ActionType::Digital);
input.registerAction("Sprint",      engine::ActionType::Digital);
input.registerAction("Reload",      engine::ActionType::Digital);
input.registerAction("Interact",    engine::ActionType::Digital);
input.registerAction("MoveForward", engine::ActionType::Analog1D);
input.registerAction("MoveStrafe",  engine::ActionType::Analog1D);
input.registerAction("LookYaw",     engine::ActionType::Analog1D);
input.registerAction("LookPitch",   engine::ActionType::Analog1D);
input.registerAction("WeaponSlot1", engine::ActionType::Digital);
input.registerAction("WeaponSlot2", engine::ActionType::Digital);
input.registerAction("WeaponSlot3", engine::ActionType::Digital);
input.registerAction("ScoreBoard",  engine::ActionType::Digital);  // UI hold
input.registerAction("Chat",        engine::ActionType::Digital);  // UI momentary
```

Action names are the keys used everywhere: in `input.toml`, in C++ queries, and in the serialized `InputFrame`.

### 2.3 ActionState

Querying an action returns an `ActionState`:

```cpp
struct ActionState
{
    // Digital
    bool held       = false;   // true every tick the action is active
    bool justPressed  = false; // true only the first tick it becomes active
    bool justReleased = false; // true only the first tick it becomes inactive

    // Analog
    float value     = 0.f;    // current 1D value; [-1,1] for axes, [0,1] for triggers
    float delta     = 0.f;    // change since last tick (useful for mouse axes)
    engine::Vec2 value2D{};   // only valid for Analog2D actions
};
```

---

## 3. InputBinding: Mapping Physical Inputs to Actions

A binding maps one physical input source to one action, with optional modifiers.

### 3.1 Binding Sources

```cpp
enum class InputDevice { Keyboard, Mouse, Gamepad };

// Keyboard source
struct KeyboardBinding {
    engine::Key key;          // e.g. Key::Space, Key::LeftControl, Key::R
};

// Mouse button source
struct MouseButtonBinding {
    engine::MouseButton button; // Left, Right, Middle, X1, X2
};

// Mouse axis source
struct MouseAxisBinding {
    engine::MouseAxis axis;     // X, Y, ScrollWheel
    float             scale;    // multiply raw delta; negative to invert
};

// Gamepad button source
struct GamepadButtonBinding {
    engine::GamepadButton button; // A, B, X, Y, LB, RB, LT, RT (digital threshold), etc.
    int                   playerIndex = 0;
};

// Gamepad axis source
struct GamepadAxisBinding {
    engine::GamepadAxis axis;     // LeftStickX, LeftStickY, RightStickX, RightStickY,
                                  // LeftTrigger, RightTrigger
    float               deadzone  = 0.15f;
    float               scale     = 1.f;
    int                 playerIndex = 0;
};
```

### 3.2 Multiple Bindings Per Action

Any action can have an arbitrary number of bindings. The engine ORs digital sources and sums (clamped) analog sources:

```cpp
// Both Left Mouse and Gamepad Right Trigger fire the "Fire" action
input.bindAction("Fire", MouseButtonBinding{ engine::MouseButton::Left  });
input.bindAction("Fire", GamepadButtonBinding{ engine::GamepadButton::RightTrigger });

// WASD mapped to 1D analog actions
input.bindAction("MoveForward", KeyboardBinding{ engine::Key::W }, +1.f);
input.bindAction("MoveForward", KeyboardBinding{ engine::Key::S }, -1.f);
input.bindAction("MoveStrafe",  KeyboardBinding{ engine::Key::D }, +1.f);
input.bindAction("MoveStrafe",  KeyboardBinding{ engine::Key::A }, -1.f);
```

---

## 4. Defining Bindings in `input.toml`

Bindings can be (and by default should be) declared in `config/input.toml`. C++ code bindings supplement or override the file. The file is read once at startup; runtime changes are applied through the rebinding API (see Section 10).

### 4.1 File Format

```toml
# config/input.toml
# Syntax: [[bindings]] table array. Each entry maps one physical source to one action.
# device: "keyboard" | "mouse_button" | "mouse_axis" | "gamepad_button" | "gamepad_axis"

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
scale  = 1.0          # sensitivity is applied separately (see Section 11)

[[bindings]]
action = "LookPitch"
device = "mouse_axis"
axis   = "y"
scale  = -1.0         # invert Y: mouse up = look up

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

# --- Gamepad fallback bindings (active whenever a gamepad is connected) ---

[[bindings]]
action        = "Fire"
device        = "gamepad_button"
button        = "right_trigger"
player_index  = 0

[[bindings]]
action        = "Jump"
device        = "gamepad_button"
button        = "a"
player_index  = 0

[[bindings]]
action        = "MoveForward"
device        = "gamepad_axis"
axis          = "left_stick_y"
deadzone      = 0.15
scale         = 1.0
player_index  = 0

[[bindings]]
action        = "LookYaw"
device        = "gamepad_axis"
axis          = "right_stick_x"
deadzone      = 0.12
scale         = 1.0
player_index  = 0
```

### 4.2 Key Name Reference

Keyboard key names in TOML are lowercase, with spaces replaced by underscores:

```
a-z, 0-9
space, enter, escape, tab, backspace, delete
left_shift, right_shift, left_control, right_control, left_alt, right_alt
f1-f12
left, right, up, down
page_up, page_down, home, end, insert
num0-num9, numpad_add, numpad_subtract, numpad_multiply, numpad_divide
```

---

## 5. Defining Bindings in C++

C++ bindings are useful for:
- Programmatic defaults applied after loading the TOML (e.g., bindings that depend on game state)
- Secondary/fallback bindings that should never be player-rebindable
- Bindings constructed from data files other than TOML (e.g., a mod system)

```cpp
// Supplement TOML bindings in onInit
void MyGame::onInit(engine::GameContext& ctx)
{
    engine::InputSystem& input = ctx.input;

    // Register all actions first (TOML bindings are validated against registered actions)
    registerAllActions(input);

    // Load TOML (this also applies all [[bindings]] entries in the file)
    input.loadBindingsFromFile("config/input.toml");

    // Optionally add C++ bindings on top
    input.bindAction("ScoreBoard",
        engine::KeyboardBinding{ engine::Key::Tab });
}
```

To clear bindings for an action before re-applying (e.g., on settings apply):

```cpp
input.clearBindingsForAction("Fire");
input.bindAction("Fire", engine::MouseButtonBinding{ engine::MouseButton::Left });
```

---

## 6. InputReceiver: ECS Component and Focus/Priority

### 6.1 The Component

`InputReceiverComponent` marks an entity as a consumer of input events. Without this component, an entity's systems can still call `InputSystem::queryAction()` globally, but contextual input routing (which entity gets which actions) depends on this component.

```cpp
struct InputReceiverComponent
{
    engine::PlayerId playerId     = engine::kInvalidPlayerId;
    int              priority     = 0;     // higher wins focus conflicts
    bool             consumesInput = true;  // if false, input "passes through"
    engine::InputFocusGroup group = engine::InputFocusGroup::Gameplay;
};
```

### 6.2 Focus Groups

Focus determines which `InputReceiverComponent` entities actually receive input this tick.

| FocusGroup | Meaning |
|---|---|
| `Gameplay` | Normal in-game player entity focus |
| `UI` | Menus, chat, pause screen |
| `Console` | Debug console overlay |
| `Cutscene` | Scripted sequence, no player input |

The engine maintains a focus stack. When a UI entity (e.g., chat box) gains focus, it is pushed onto the stack and gameplay entities stop receiving input. When the UI is dismissed, it is popped and gameplay resumes.

```cpp
// Activate chat UI (steals focus from player)
ctx.input.pushFocusGroup(engine::InputFocusGroup::UI);

// Close chat UI (returns focus to gameplay)
ctx.input.popFocusGroup(engine::InputFocusGroup::UI);
```

### 6.3 Focus Arbitration Within a Group

When multiple entities share the same focus group and the same `PlayerId`, the one with the highest `priority` value receives `justPressed`/`justReleased` events first. If `consumesInput` is true, lower-priority entities in the same group do not receive the same event that tick.

---

## 7. Consuming Input in Systems

### 7.1 Polling

Polling is the standard pattern for continuous actions (movement, looking, firing):

```cpp
void PlayerMovementSystem::update(engine::World& world, float dt)
{
    world.query<engine::InputReceiverComponent, engine::CharacterControllerComponent>(
        [dt](engine::EntityId,
             const engine::InputReceiverComponent& receiver,
             engine::CharacterControllerComponent& cc)
        {
            engine::InputSystem& input = engine::InputSystem::get();

            float fwd    = input.queryAnalog1D("MoveForward", receiver.playerId);
            float strafe = input.queryAnalog1D("MoveStrafe",  receiver.playerId);
            bool  jump   = input.queryJustPressed("Jump",     receiver.playerId);
            bool  sprint = input.queryHeld("Sprint",          receiver.playerId);

            cc.wishDir = engine::Vec3(strafe, 0.f, fwd).normalized();
            cc.wantJump   = jump;
            cc.isSprinting = sprint;
        });
}
```

### 7.2 Event Callbacks

For one-shot actions (reload, weapon switch, interact), register callbacks. Callbacks fire during the input-collection phase, before systems run.

```cpp
// In onInit:
ctx.input.onActionPressed("Reload", [this](engine::PlayerId pid) {
    // Trigger reload animation, begin reload timer
    triggerReload(pid);
});

ctx.input.onActionPressed("WeaponSlot1", [this](engine::PlayerId pid) {
    switchWeaponSlot(pid, 0);
});
```

Callbacks are invoked on the game thread (during the fixed-tick input collection step), not on a background thread. Do not issue GPU commands from a callback.

### 7.3 Consuming vs. Passing Through

By default, a digital action is "consumed" by the highest-priority receiver. Lower-priority receivers see `held = false` for that tick. Set `consumesInput = false` on a receiver to make it observe-only (e.g., a global hotkey system that should fire even when a menu is open):

```cpp
// Screenshot key works regardless of UI focus
engine::InputReceiverComponent screenshotReceiver{};
screenshotReceiver.group         = engine::InputFocusGroup::UI;
screenshotReceiver.consumesInput = false;
screenshotReceiver.priority      = -100;
ctx.world.addComponent<engine::InputReceiverComponent>(screenshotEntity, screenshotReceiver);
```

---

## 8. Networked Input: InputFrame and Server Submission

In the server-authoritative model, the client does not send key/mouse events directly. Instead, at the end of each fixed game tick the input system packages the current action state into an `InputFrame` and sends it to the server.

### 8.1 InputFrame Layout

```cpp
struct InputFrame
{
    uint32_t tick;            // client's fixed-tick counter when this was sampled
    float    clientTimestamp; // wall-clock seconds (for lag compensation)

    // Digital actions packed as a bitfield (up to 64 named actions)
    uint64_t digitalHeld;
    uint64_t digitalJustPressed;

    // Analog actions (variable count, up to 16 axes)
    uint8_t  analogCount;
    struct AnalogEntry {
        uint8_t actionIndex;
        float   value;
    } analogs[16];

    // High-precision look delta (raw mouse / right stick) for this tick
    float lookYawDelta;
    float lookPitchDelta;
};
```

The `InputFrame` is compact (< 64 bytes for a typical FPS layout). It is sent via the reliability layer using the `Unreliable` channel (dropping a single frame is acceptable; the next frame supersedes it). If a frame is dropped, the server uses the last known `InputFrame` for that client.

### 8.2 Transmission Timing

The network system sends input frames once per fixed game tick (64 Hz). Server-to-client snapshots go in the opposite direction at ~20 Hz. This asymmetry is intentional:

- High-frequency input upload ensures the server has the freshest available inputs.
- Lower-frequency snapshot download reduces bandwidth while still providing enough data for smooth interpolation.

### 8.3 Server-Side Processing

The server processes `InputFrame` packets in `NetworkSystem::receive()` (early in the fixed tick), queuing them into per-client input buffers. The `PlayerMovementSystem` and `WeaponSystem` on the server side read from these buffers — not from a local hardware device.

The engine provides `NetworkedInputComponent` which the server attaches to player entities:

```cpp
struct NetworkedInputComponent
{
    engine::InputFrame current;   // most recently applied frame
    engine::InputFrame pending;   // next frame received from client
    float              clientRtt; // smoothed RTT in seconds
};
```

Game systems can query this component the same way they query `InputReceiverComponent` — the query abstraction is identical. The difference is invisible at the system level.

---

## 9. Client-Side Prediction with Input

Client-side prediction allows the local player to see the result of their input immediately, without waiting one round-trip for server confirmation. The prediction and reconciliation mechanism is described in detail in `task-07-gameplay-loop.md`; this section covers the input system's role.

### 9.1 Input History Buffer

The client maintains a circular buffer of recent `InputFrame`s (default 128 ticks = 2 seconds at 64 Hz):

```cpp
// Managed internally by engine::NetworkSystem; exposed read-only
const engine::InputFrame* getInputHistoryAt(uint32_t tick) const;
```

When a server correction arrives with an acknowledged tick `T`, the client:

1. Rolls back entity state to tick `T`.
2. Replays all input frames from tick `T` to the current local tick by calling through the same system update path.
3. Blends the corrected position toward the predicted position over a short window (see `task-07-gameplay-loop.md`).

Game systems do not need to implement any special replay logic as long as they are deterministic and read only from ECS components — the engine's prediction layer re-runs the system update function with historical frames.

### 9.2 Non-Predicted Input

Some input-driven actions should not be predicted because the outcome is highly server-dependent (e.g., purchasing an item in a buy phase, voting). Mark these actions as non-predicted:

```cpp
input.registerAction("BuyWeapon", engine::ActionType::Digital,
    engine::ActionFlags::NonPredicted);
```

Non-predicted actions are still sent in `InputFrame` but are not applied locally. The client waits for server confirmation before any state change.

---

## 10. Runtime Rebinding and Player Settings

### 10.1 Rebinding API

```cpp
// Remove existing bindings for an action and replace with a new one
void InputSystem::rebindAction(
    std::string_view       actionName,
    engine::PhysicalInput  newBinding);   // variant<KeyboardBinding, MouseButtonBinding, ...>

// Query what is currently bound to an action
std::vector<engine::PhysicalInput> InputSystem::getBindingsForAction(
    std::string_view actionName) const;

// Save current bindings back to input.toml (replaces file)
void InputSystem::saveBindingsToFile(std::string_view path) const;

// Reset all bindings to their defaults (as loaded from the original TOML)
void InputSystem::resetToDefaults();
```

### 10.2 Conflict Detection

When the player tries to bind a key that is already used:

```cpp
std::string_view existing = input.getActionBoundTo(engine::Key::R);
if (!existing.empty()) {
    // "R" is already bound to "Reload" — show conflict dialog
    showRebindConflict(existing, "Reload");
} else {
    input.rebindAction("Reload", engine::KeyboardBinding{ engine::Key::R });
}
```

Conflicts are only flagged within the same focus group. A key can simultaneously be bound in `Gameplay` and `UI` groups without conflict.

### 10.3 Saving Rebindings

Player rebindings are saved through the `SaveSystem` (see `task-08-save-system.md`). The player profile stores a rebinding blob alongside other settings. On startup, the rebindings from the profile override the defaults from `input.toml`:

```cpp
// In onInit, after loading input.toml defaults:
engine::PlayerProfile profile = ctx.save.loadProfile(localPlayerId);
if (profile.hasCustomBindings()) {
    input.applyBindingOverrides(profile.bindingOverrides());
}
```

---

## 11. FPS-Specific Patterns

### 11.1 Raw Mouse Input

The engine defaults to Win32 raw input (`WM_INPUT`) for mouse, bypassing the OS cursor acceleration curve. This is strongly recommended for FPS games. Configure in `config/game.toml`:

```toml
[input.mouse]
raw_input     = true    # use WM_INPUT, bypass OS acceleration
capture       = true    # confine cursor to window, hide it
```

Raw input gives `delta` values in raw device units (typically counts/tick with no scaling). The engine does not apply any smoothing by default — smoothing is the game's choice.

### 11.2 Sensitivity and Look Speed

Mouse sensitivity is not a binding property — it is a scalar applied at the action-query level. The engine exposes a per-axis sensitivity value:

```cpp
// In player settings apply:
ctx.input.setAxisSensitivity("LookYaw",   settings.mouseSensitivity);
ctx.input.setAxisSensitivity("LookPitch", settings.mouseSensitivity * settings.pitchScale);
```

The look system should apply sensitivity before any FOV-dependent compensation. Recommended implementation:

```cpp
// In CameraSystem (render group)
float yawDelta   = input.queryDelta("LookYaw",   pid);   // already sensitivity-scaled
float pitchDelta = input.queryDelta("LookPitch",  pid);

// Frame-rate-independent: raw mouse delta is already in absolute units per tick,
// not a rate — do NOT multiply by dt for mouse look.
// For gamepad stick (which is a rate), multiply by dt:
float stickYaw = input.queryAnalog1D("LookYaw", pid) * gamepadLookSpeed * dt;
```

### 11.3 Frame-Rate-Independent Look Speed

Mouse deltas from `queryDelta()` are already in absolute units accumulated over one fixed tick (15.625 ms). They are not per-second rates, so do not scale by `dt`. This is correct:

```cpp
yaw   += input.queryDelta("LookYaw",   pid);   // correct — delta is already per-tick
pitch += input.queryDelta("LookPitch", pid);   // correct
```

This is incorrect:

```cpp
yaw   += input.queryDelta("LookYaw",   pid) * dt;   // WRONG — double-integrates time
```

Gamepad sticks return a normalized rate `[-1, 1]`. For gamepad look, multiply by a look-speed constant and `dt`:

```cpp
yaw   += input.queryAnalog1D("LookYaw",   pid) * kGamepadLookSpeedDegPerSec * dt;
pitch += input.queryAnalog1D("LookPitch", pid) * kGamepadLookSpeedDegPerSec * dt;
```

The engine's `InputSystem` internally accumulates mouse deltas between fixed ticks to ensure no raw counts are lost even if the fixed tick fires less than once per render frame. Deltas are reset at the start of each fixed tick after `InputFrame` is assembled.

### 11.4 ADS Sensitivity Scaling

A common FPS pattern is reducing sensitivity when aiming down sights. Implement this in the system layer by reading the `WeaponComponent` state:

```cpp
bool isADS = weapon.isAimingDownSights;
float sensMult = isADS ? settings.adsSensitivityMultiplier : 1.f;
float yawDelta = input.queryDelta("LookYaw", pid) * sensMult;
```

Do not modify `InputSystem` sensitivity for ADS — modifying the sensitivity property affects the stored `InputFrame` delta, which would corrupt server-side lag compensation. Apply ADS scaling in your camera/look system only, after reading the raw delta from `InputFrame`.

### 11.5 Input in the HUD / UI

UI systems should query input state through the `InputSystem` just like gameplay systems, but only when `InputFocusGroup::UI` is the active focus group. The engine provides a helper:

```cpp
if (ctx.input.isFocusGroupActive(engine::InputFocusGroup::UI)) {
    bool confirmPressed = ctx.input.queryJustPressed("UIConfirm", engine::kNoPlayerId);
    // ...
}
```

Define UI actions separately from gameplay actions to avoid conflicts:

```toml
[[bindings]]
action      = "UIConfirm"
device      = "keyboard"
key         = "enter"
focus_group = "ui"

[[bindings]]
action      = "UIBack"
device      = "keyboard"
key         = "escape"
focus_group = "ui"
```

---

*Document maintained by the engine team. File issues against the `engine-phase6` milestone.*
