#include <gtest/gtest.h>

#include "app/GameLoop.h"
#include "app/IGameMode.h"
#include "app/BitStream.h"
#include "app/SystemScheduler.h"
#include "app/GameContext.h"
#include <core/input/InputFrame.h>

using namespace engine::app;
using engine::core::input::InputFrame;

// ── Minimal IGameMode stub ────────────────────────────────────────────────────

struct NullGameMode : IGameMode {
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

// ── Tick counter ──────────────────────────────────────────────────────────────

TEST(GameLoop, TickCounterStartsAtZero) {
    GameLoop loop;
    EXPECT_EQ(loop.currentTick(), 0u);
}

TEST(GameLoop, ServerTickIncrementsCounter) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(loop.currentTick(), 1u);
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(loop.currentTick(), 2u);
}

TEST(GameLoop, ClientTickIncrementsCounter) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    InputFrame  frame{};
    loop.clientTick(ctx, 1.0f / 64.0f, frame);
    EXPECT_EQ(loop.currentTick(), 1u);
}

TEST(GameLoop, ClientTickStampsFrameWithPreIncrementTick) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    InputFrame  frame{};
    // Tick is 0 before clientTick → frame should be stamped 0, then tick becomes 1.
    loop.clientTick(ctx, 1.0f / 64.0f, frame);
    EXPECT_EQ(frame.tick, 0u);
    EXPECT_EQ(loop.currentTick(), 1u);

    InputFrame frame2{};
    loop.clientTick(ctx, 1.0f / 64.0f, frame2);
    EXPECT_EQ(frame2.tick, 1u);
}

// ── Snapshot generation ───────────────────────────────────────────────────────

TEST(GameLoop, NoSnapshotBeforeThreeTicks) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(loop.pendingSnapshot(), nullptr);
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(loop.pendingSnapshot(), nullptr);
}

TEST(GameLoop, SnapshotReadyAfterThreeTicks) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    for (int i = 0; i < 3; ++i)
        loop.serverTick(ctx, 1.0f / 64.0f);

    ASSERT_NE(loop.pendingSnapshot(), nullptr);
    EXPECT_EQ(loop.pendingSnapshot()->tick, 3u);
}

TEST(GameLoop, SnapshotServerTimeMatchesTick) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    for (int i = 0; i < 3; ++i)
        loop.serverTick(ctx, 1.0f / 64.0f);

    const auto* snap = loop.pendingSnapshot();
    ASSERT_NE(snap, nullptr);
    EXPECT_NEAR(snap->serverTime, 3.0f / 64.0f, 1e-5f);
}

TEST(GameLoop, SnapshotClearedAfterClearCall) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    for (int i = 0; i < 3; ++i)
        loop.serverTick(ctx, 1.0f / 64.0f);

    ASSERT_NE(loop.pendingSnapshot(), nullptr);
    loop.clearPendingSnapshot();
    EXPECT_EQ(loop.pendingSnapshot(), nullptr);
}

TEST(GameLoop, SnapshotResetsBetweenCycles) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    for (int i = 0; i < 3; ++i) loop.serverTick(ctx, 1.0f / 64.0f);
    ASSERT_NE(loop.pendingSnapshot(), nullptr);
    loop.clearPendingSnapshot();

    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(loop.pendingSnapshot(), nullptr);
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(loop.pendingSnapshot(), nullptr);

    loop.serverTick(ctx, 1.0f / 64.0f);
    ASSERT_NE(loop.pendingSnapshot(), nullptr);
    EXPECT_EQ(loop.pendingSnapshot()->tick, 6u);
}

// ── SystemScheduler integration ───────────────────────────────────────────────

TEST(GameLoop, ServerTickCallsSchedulerGroupsInOrder) {
    GameLoop       loop;
    SystemScheduler sched;

    std::vector<TickGroup> order;
    sched.registerSystem(TickGroup::PrePhysics,
                         [&](float) { order.push_back(TickGroup::PrePhysics); });
    sched.registerSystem(TickGroup::PostPhysics,
                         [&](float) { order.push_back(TickGroup::PostPhysics); });
    sched.registerSystem(TickGroup::GameFixed,
                         [&](float) { order.push_back(TickGroup::GameFixed); });

    loop.init({ &sched, nullptr, nullptr });

    GameContext ctx{};
    loop.serverTick(ctx, 1.0f / 64.0f);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], TickGroup::PrePhysics);
    EXPECT_EQ(order[1], TickGroup::PostPhysics);
    EXPECT_EQ(order[2], TickGroup::GameFixed);
}

TEST(GameLoop, ClientTickCallsSchedulerGroupsInOrder) {
    GameLoop        loop;
    SystemScheduler sched;

    std::vector<TickGroup> order;
    sched.registerSystem(TickGroup::PrePhysics,
                         [&](float) { order.push_back(TickGroup::PrePhysics); });
    sched.registerSystem(TickGroup::PostPhysics,
                         [&](float) { order.push_back(TickGroup::PostPhysics); });
    sched.registerSystem(TickGroup::GameFixed,
                         [&](float) { order.push_back(TickGroup::GameFixed); });

    loop.init({ &sched, nullptr, nullptr });

    GameContext ctx{};
    InputFrame  frame{};
    loop.clientTick(ctx, 1.0f / 64.0f, frame);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], TickGroup::PrePhysics);
    EXPECT_EQ(order[1], TickGroup::PostPhysics);
    EXPECT_EQ(order[2], TickGroup::GameFixed);
}

TEST(GameLoop, NullSchedulerIsNoop) {
    GameLoop loop;
    loop.init({ nullptr, nullptr, nullptr });

    GameContext ctx{};
    EXPECT_NO_THROW(loop.serverTick(ctx, 1.0f / 64.0f));
}

// ── IGameMode integration ─────────────────────────────────────────────────────

TEST(GameLoop, WinConditionCheckedEachServerTick) {
    struct CountingMode : NullGameMode {
        int winChecks = 0;
        bool evaluateWinCondition(GameContext&) override { ++winChecks; return false; }
    } mode;

    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    loop.serverTick(ctx, 1.0f / 64.0f);
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(mode.winChecks, 2);
}

TEST(GameLoop, RoundTickCalledEachServerTick) {
    struct CountingMode : NullGameMode {
        int roundTicks = 0;
        void onRoundTick(GameContext&, float) override { ++roundTicks; }
    } mode;

    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    loop.serverTick(ctx, 1.0f / 64.0f);
    loop.serverTick(ctx, 1.0f / 64.0f);
    loop.serverTick(ctx, 1.0f / 64.0f);
    EXPECT_EQ(mode.roundTicks, 3);
}

TEST(GameLoop, SnapshotContainsGameModeState) {
    struct StatefulMode : NullGameMode {
        void serializeState(BitStreamWriter& bsw) const override {
            bsw.writeU8(0xAB);
            bsw.writeU32(0xDEADBEEFu);
        }
    } mode;

    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    for (int i = 0; i < 3; ++i)
        loop.serverTick(ctx, 1.0f / 64.0f);

    const auto* snap = loop.pendingSnapshot();
    ASSERT_NE(snap, nullptr);
    ASSERT_EQ(snap->gameModeState.size, 5u);  // 1 byte + 4 bytes
    EXPECT_EQ(snap->gameModeState.data[0], 0xABu);
    // Bytes 1-4 encode 0xDEADBEEF little-endian
    EXPECT_EQ(snap->gameModeState.data[1], 0xEFu);
    EXPECT_EQ(snap->gameModeState.data[2], 0xBEu);
    EXPECT_EQ(snap->gameModeState.data[3], 0xADu);
    EXPECT_EQ(snap->gameModeState.data[4], 0xDEu);
}

// ── BitStream round-trip ──────────────────────────────────────────────────────

TEST(BitStream, WriterReaderRoundTripU8) {
    BitStreamWriter bsw;
    bsw.writeU8(0xCD);
    BitStreamReader bsr(bsw.data(), bsw.byteSize());
    EXPECT_EQ(bsr.readU8(), 0xCDu);
    EXPECT_TRUE(bsr.ok());
}

TEST(BitStream, WriterReaderRoundTripU32) {
    BitStreamWriter bsw;
    bsw.writeU32(0x12345678u);
    BitStreamReader bsr(bsw.data(), bsw.byteSize());
    EXPECT_EQ(bsr.readU32(), 0x12345678u);
    EXPECT_TRUE(bsr.ok());
}

TEST(BitStream, WriterReaderRoundTripF32) {
    BitStreamWriter bsw;
    bsw.writeF32(2.71828f);
    BitStreamReader bsr(bsw.data(), bsw.byteSize());
    EXPECT_NEAR(bsr.readF32(), 2.71828f, 1e-5f);
    EXPECT_TRUE(bsr.ok());
}

TEST(BitStream, WriterReaderRoundTripBool) {
    BitStreamWriter bsw;
    bsw.writeBool(true);
    bsw.writeBool(false);
    BitStreamReader bsr(bsw.data(), bsw.byteSize());
    EXPECT_TRUE(bsr.readBool());
    EXPECT_FALSE(bsr.readBool());
    EXPECT_TRUE(bsr.ok());
}

TEST(BitStream, OverreadSetsOkFalse) {
    BitStreamWriter bsw;
    bsw.writeU8(0x01);
    BitStreamReader bsr(bsw.data(), bsw.byteSize());
    bsr.readU8();
    bsr.readU8();  // overread
    EXPECT_FALSE(bsr.ok());
}

TEST(BitStream, SkipAdvancesPosition) {
    BitStreamWriter bsw;
    bsw.writeU8(0x00);
    bsw.writeU8(0xAB);
    BitStreamReader bsr(bsw.data(), bsw.byteSize());
    bsr.skip(1);
    EXPECT_EQ(bsr.readU8(), 0xABu);
    EXPECT_TRUE(bsr.ok());
}
