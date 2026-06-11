#include <gtest/gtest.h>

#include "app/GameLoop.h"
#include "app/IGameMode.h"
#include "app/BitStream.h"
#include "app/GameContext.h"
#include <core/EventBus.h>
#include <core/ecs/Entity.h>
#include <physics/TriggerEvents.h>

using namespace engine::app;
using engine::core::ecs::Entity;
using engine::core::ecs::kInvalidEntity;

// ── Minimal IGameMode stub ────────────────────────────────────────────────────

struct NullMode : IGameMode {
    void onRoundStart(GameContext&) override {}
    void onRoundTick(GameContext&, float) override {}
    void onRoundEnd(GameContext&) override {}
    void onMatchEnd(GameContext&) override {}
    void onPlayerJoin(GameContext&, uint32_t) override {}
    void onPlayerLeave(GameContext&, uint32_t) override {}
    void onPlayerSpawn(GameContext&, uint32_t) override {}
    void onPlayerDeath(GameContext&, uint32_t, uint32_t) override {}
    void serializeState(BitStreamWriter&) const override {}
    void deserializeState(BitStreamReader&) override {}
    bool evaluateWinCondition(GameContext&) override { return false; }
};

// ── MockGameMode recording trigger callbacks ──────────────────────────────────

struct MockGameMode : NullMode {
    Entity   lastTrigger  = kInvalidEntity;
    Entity   lastOther    = kInvalidEntity;
    int      enterCount   = 0;
    Entity   lastExitTrigger = kInvalidEntity;
    Entity   lastExitOther   = kInvalidEntity;
    int      exitCount    = 0;

    void onTriggerEnter(Entity t, Entity o) override {
        lastTrigger = t;
        lastOther   = o;
        ++enterCount;
    }
    void onTriggerExit(Entity t, Entity o) override {
        lastExitTrigger = t;
        lastExitOther   = o;
        ++exitCount;
    }
};

// ── Helper: make a valid Entity from raw index ────────────────────────────────
static Entity makeEntity(uint32_t index) {
    Entity e{};
    e.index      = index;
    e.generation = 1;
    return e;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(TriggerCallbacks, EnterEventDispatchedToGameMode) {
    MockGameMode       mock;
    engine::core::EventBus bus;

    GameLoop loop;
    GameLoop::Desc desc{};
    desc.eventBus = &bus;
    desc.gameMode = &mock;
    loop.init(desc);

    const Entity E1 = makeEntity(1);
    const Entity E2 = makeEntity(2);

    engine::physics::TriggerEnterEvent ev{};
    ev.triggerEntity   = E1;
    ev.enteringEntity  = E2;
    bus.publish(ev);

    EXPECT_EQ(mock.enterCount,   1);
    EXPECT_EQ(mock.lastTrigger,  E1);
    EXPECT_EQ(mock.lastOther,    E2);
}

TEST(TriggerCallbacks, ExitEventDispatchedToGameMode) {
    MockGameMode       mock;
    engine::core::EventBus bus;

    GameLoop loop;
    GameLoop::Desc desc{};
    desc.eventBus = &bus;
    desc.gameMode = &mock;
    loop.init(desc);

    const Entity E3 = makeEntity(3);
    const Entity E4 = makeEntity(4);

    engine::physics::TriggerExitEvent ev{};
    ev.triggerEntity  = E3;
    ev.leavingEntity  = E4;
    bus.publish(ev);

    EXPECT_EQ(mock.exitCount,        1);
    EXPECT_EQ(mock.lastExitTrigger,  E3);
    EXPECT_EQ(mock.lastExitOther,    E4);
}

TEST(TriggerCallbacks, NoDispatchWithoutGameMode) {
    engine::core::EventBus bus;

    GameLoop loop;
    GameLoop::Desc desc{};
    desc.eventBus = &bus;
    desc.gameMode = nullptr;
    loop.init(desc);

    engine::physics::TriggerEnterEvent ev{};
    ev.triggerEntity  = makeEntity(5);
    ev.enteringEntity = makeEntity(6);

    // Should not crash when gameMode_ is null.
    EXPECT_NO_THROW(bus.publish(ev));
}

TEST(TriggerCallbacks, NoDispatchWithoutEventBus) {
    MockGameMode mock;

    GameLoop loop;
    GameLoop::Desc desc{};
    desc.eventBus = nullptr;
    desc.gameMode = &mock;
    loop.init(desc);

    // No bus → no subscription → enterCount stays 0, nothing to verify but no crash.
    EXPECT_EQ(mock.enterCount, 0);
    EXPECT_EQ(mock.exitCount,  0);
}

TEST(TriggerCallbacks, MultipleEventsAllDispatched) {
    MockGameMode       mock;
    engine::core::EventBus bus;

    GameLoop loop;
    GameLoop::Desc desc{};
    desc.eventBus = &bus;
    desc.gameMode = &mock;
    loop.init(desc);

    for (uint32_t i = 0; i < 5; ++i) {
        engine::physics::TriggerEnterEvent ev{};
        ev.triggerEntity  = makeEntity(10 + i);
        ev.enteringEntity = makeEntity(20 + i);
        bus.publish(ev);
    }

    EXPECT_EQ(mock.enterCount, 5);
}
