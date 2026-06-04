#include <gtest/gtest.h>
#include "core/input/InputSystem.h"
#include "core/input/InputAction.h"
#include "core/input/InputBinding.h"
#include "core/input/InputFrame.h"
#include "core/input/InputReceiverComponent.h"

using namespace engine::core::input;

// ── InputFrame ────────────────────────────────────────────────────────────────

TEST(InputFrame, DefaultValues) {
    InputFrame f;
    EXPECT_EQ(f.tick, 0u);
    EXPECT_EQ(f.clientTimestamp, 0u);
    EXPECT_EQ(f.digitalHeld, 0u);
    EXPECT_EQ(f.digitalJustPressed, 0u);
    EXPECT_NEAR(f.lookYawDelta, 0.0f, 1e-6f);
    EXPECT_NEAR(f.lookPitchDelta, 0.0f, 1e-6f);
}

// ── InputReceiverComponent ────────────────────────────────────────────────────

TEST(InputReceiverComponent, ComponentIdIs2) {
    EXPECT_EQ(InputReceiverComponent::kComponentId, engine::core::ecs::ComponentTypeId{2});
}

TEST(InputReceiverComponent, DefaultValues) {
    InputReceiverComponent c;
    EXPECT_EQ(c.playerId, 0u);
    EXPECT_EQ(c.priority, 0u);
    EXPECT_FALSE(c.consumesInput);
    EXPECT_EQ(c.focusGroup, FocusGroup::Gameplay);
}

// ── ActionState ───────────────────────────────────────────────────────────────

TEST(ActionState, DefaultValues) {
    ActionState s;
    EXPECT_EQ(s.type, InputActionType::Digital);
    EXPECT_FALSE(s.held);
    EXPECT_FALSE(s.justPressed);
    EXPECT_FALSE(s.justReleased);
    EXPECT_NEAR(s.value,  0.0f, 1e-6f);
    EXPECT_NEAR(s.valueX, 0.0f, 1e-6f);
    EXPECT_NEAR(s.valueY, 0.0f, 1e-6f);
}

// ── InputSystem ───────────────────────────────────────────────────────────────

TEST(InputSystem, UnknownActionReturnsFalse) {
    InputSystem sys;
    sys.update();
    EXPECT_FALSE(sys.queryHeld("fire"));
    EXPECT_FALSE(sys.queryJustPressed("fire"));
    EXPECT_NEAR(sys.queryAnalog1D("fire"), 0.0f, 1e-6f);
}

TEST(InputSystem, QueryDefaultStateAfterBind) {
    // No keys are physically pressed in a headless test — states are false/zero.
    InputSystem sys;
    sys.bindAction({"Jump", InputActionType::Digital, engine::core::Key::Space});
    sys.update();
    EXPECT_FALSE(sys.queryHeld("Jump"));
    EXPECT_FALSE(sys.queryJustPressed("Jump"));
}

TEST(InputSystem, Analog1DDefaultIsZero) {
    InputSystem sys;
    sys.bindAction({"MoveForward", InputActionType::Analog1D, engine::core::Key::W, 1.0f});
    sys.update();
    EXPECT_NEAR(sys.queryAnalog1D("MoveForward"), 0.0f, 1e-6f);
}

TEST(InputSystem, ClearBindingsRemovesAction) {
    InputSystem sys;
    sys.bindAction({"Fire", InputActionType::Digital, engine::core::Key::MouseLeft});
    sys.clearBindingsForAction("Fire");
    sys.update();
    // After clear the action has no state entry
    EXPECT_FALSE(sys.queryHeld("Fire"));
    EXPECT_FALSE(sys.queryJustPressed("Fire"));
}

TEST(InputSystem, ClearUnknownActionIsNoop) {
    InputSystem sys;
    EXPECT_NO_THROW(sys.clearBindingsForAction("nonexistent"));
}

TEST(InputSystem, MultipleBindingsSameAction) {
    InputSystem sys;
    sys.bindAction({"Move", InputActionType::Digital, engine::core::Key::W});
    sys.bindAction({"Move", InputActionType::Digital, engine::core::Key::Up});
    sys.update();
    EXPECT_FALSE(sys.queryHeld("Move"));
}

TEST(InputSystem, QueryReturnsCorrectType) {
    InputSystem sys;
    sys.bindAction({"Sprint", InputActionType::Digital, engine::core::Key::LShift});
    sys.update();
    ActionState s = sys.query("Sprint");
    EXPECT_EQ(s.type, InputActionType::Digital);
}

TEST(InputSystem, QueryAnalog1DTypeIsSet) {
    InputSystem sys;
    sys.bindAction({"Throttle", InputActionType::Analog1D, engine::core::Key::Up, 1.0f});
    sys.update();
    ActionState s = sys.query("Throttle");
    EXPECT_EQ(s.type, InputActionType::Analog1D);
}

TEST(InputSystem, LoadFromMissingTomlIsGraceful) {
    InputSystem sys;
    // Missing file → logs a warning but must not crash or throw.
    sys.loadFromToml("this_file_does_not_exist.toml");
    sys.update();
    EXPECT_FALSE(sys.queryHeld("anything"));
}

TEST(InputSystem, UpdateIdempotent) {
    InputSystem sys;
    sys.bindAction({"Crouch", InputActionType::Digital, engine::core::Key::LCtrl});
    sys.update();
    sys.update();  // second update in same frame must not crash
    EXPECT_FALSE(sys.queryHeld("Crouch"));
}

// ── loadBindingsFromToml ──────────────────────────────────────────────────────

TEST(LoadBindingsFromToml, MissingFileReturnsEmpty) {
    auto bindings = loadBindingsFromToml("nonexistent_input.toml");
    EXPECT_TRUE(bindings.empty());
}
