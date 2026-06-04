#include <gtest/gtest.h>
#include "app/SystemScheduler.h"
#include "app/IGame.h"
#include "app/GameContext.h"

using namespace engine::app;

// ── SystemScheduler ───────────────────────────────────────────────────────────

TEST(SystemScheduler, SingleSystemCalled) {
    SystemScheduler sched;
    int n = 0;
    sched.registerSystem(TickGroup::GameFixed, [&](float) { ++n; });
    sched.tickGroup(TickGroup::GameFixed, 0.016f);
    EXPECT_EQ(n, 1);
}

TEST(SystemScheduler, MultipleSystemsInGroupAllCalled) {
    SystemScheduler sched;
    int sum = 0;
    sched.registerSystem(TickGroup::PrePhysics, [&](float) { sum += 1; });
    sched.registerSystem(TickGroup::PrePhysics, [&](float) { sum += 2; });
    sched.tickGroup(TickGroup::PrePhysics, 0.016f);
    EXPECT_EQ(sum, 3);
}

TEST(SystemScheduler, DtPassedToSystem) {
    SystemScheduler sched;
    float received = 0.f;
    sched.registerSystem(TickGroup::Render, [&](float dt) { received = dt; });
    sched.tickGroup(TickGroup::Render, 0.0156f);
    EXPECT_NEAR(received, 0.0156f, 1e-6f);
}

TEST(SystemScheduler, WrongGroupNotDispatched) {
    SystemScheduler sched;
    int n = 0;
    sched.registerSystem(TickGroup::Network, [&](float) { ++n; });
    sched.tickGroup(TickGroup::Render, 0.016f);
    EXPECT_EQ(n, 0);
}

TEST(SystemScheduler, EmptyGroupIsNoop) {
    SystemScheduler sched;
    EXPECT_NO_THROW(sched.tickGroup(TickGroup::PostPhysics, 0.016f));
}

TEST(SystemScheduler, AllGroupsIndependent) {
    SystemScheduler sched;
    std::vector<TickGroup> fired;
    auto track = [&](TickGroup g) { return [&, g](float) { fired.push_back(g); }; };

    sched.registerSystem(TickGroup::PrePhysics,  track(TickGroup::PrePhysics));
    sched.registerSystem(TickGroup::PostPhysics, track(TickGroup::PostPhysics));
    sched.registerSystem(TickGroup::GameFixed,   track(TickGroup::GameFixed));
    sched.registerSystem(TickGroup::Render,      track(TickGroup::Render));
    sched.registerSystem(TickGroup::Network,     track(TickGroup::Network));

    for (auto g : {TickGroup::PrePhysics, TickGroup::PostPhysics,
                   TickGroup::GameFixed,  TickGroup::Network, TickGroup::Render})
        sched.tickGroup(g, 0.016f);

    ASSERT_EQ(fired.size(), 5u);
    EXPECT_EQ(fired[0], TickGroup::PrePhysics);
    EXPECT_EQ(fired[1], TickGroup::PostPhysics);
    EXPECT_EQ(fired[2], TickGroup::GameFixed);
    EXPECT_EQ(fired[3], TickGroup::Network);
    EXPECT_EQ(fired[4], TickGroup::Render);
}

// ── IGame mock ────────────────────────────────────────────────────────────────

struct MockGame : IGame {
    std::vector<std::string> calls;
    void onInit(GameContext&)               override { calls.push_back("init"); }
    void onGameTick(GameContext&, float)    override { calls.push_back("gameTick"); }
    void onRenderTick(GameContext&, float)  override { calls.push_back("renderTick"); }
    void onShutdown(GameContext&)           override { calls.push_back("shutdown"); }
    void onDebugUI(GameContext&)            override { calls.push_back("debugUI"); }
};

TEST(IGame, ConcreteImplCanBeInstantiated) {
    MockGame g;
    GameContext ctx;
    g.onInit(ctx);
    g.onGameTick(ctx, 0.016f);
    g.onRenderTick(ctx, 0.016f);
    g.onShutdown(ctx);
    ASSERT_EQ(g.calls.size(), 4u);
    EXPECT_EQ(g.calls[0], "init");
    EXPECT_EQ(g.calls[1], "gameTick");
    EXPECT_EQ(g.calls[2], "renderTick");
    EXPECT_EQ(g.calls[3], "shutdown");
}

TEST(IGame, DebugUIHasDefaultImpl) {
    struct NoDebugUI : IGame {
        void onInit(GameContext&)              override {}
        void onGameTick(GameContext&, float)   override {}
        void onRenderTick(GameContext&, float) override {}
        void onShutdown(GameContext&)          override {}
        // onDebugUI NOT overridden — uses base default
    } g;
    GameContext ctx;
    EXPECT_NO_THROW(g.onDebugUI(ctx));
}

