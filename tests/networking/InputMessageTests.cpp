// InputMessageTests.cpp
//
// Unit tests for:
//   - InputMessage wire round-trip (write + read all fields)
//   - toWire() float-to-int quantization
//   - GameLoop input ring buffer behaviour

#include <gtest/gtest.h>

#include <networking/InputMessage.h>
#include <networking/InputMessageSerializer.h>
#include <networking/InputMessageConvert.h>
#include <networking/Serializer.h>
#include <core/input/InputFrame.h>
#include <app/GameLoop.h>
#include <app/GameContext.h>
#include <app/BitStream.h>
#include <app/IGameMode.h>
#include <app/SystemScheduler.h>

#include <array>
#include <cstdint>

using engine::networking::InputMessage;
using engine::networking::BitWriter;
using engine::networking::BitReader;
using engine::networking::toWire;
using engine::core::input::InputFrame;
using engine::app::GameLoop;
using engine::app::GameContext;

// ---------------------------------------------------------------------------
// Round-trip: write then read back all fields
// ---------------------------------------------------------------------------

TEST(InputMessageRoundTrip, AllFieldsPreserved) {
    InputMessage sent;
    sent.tick        = 0xDEADBEEFu;
    sent.moveX       = -42;
    sent.moveZ       = 100;
    sent.yawDelta    = -1234;
    sent.pitchDelta  = 5678;
    sent.buttons     = 0x0Fu;
    sent.fireSerial  = 0xABu;

    std::array<uint8_t, 64> buf{};
    BitWriter writer(buf);
    engine::networking::writeInputMessage(writer, sent);

    ASSERT_FALSE(writer.overflow()) << "Write must not overflow a 64-byte buffer";

    BitReader reader(writer.writtenData());
    const InputMessage got = engine::networking::readInputMessage(reader);

    EXPECT_FALSE(reader.overflow()) << "Read must not overflow";
    EXPECT_EQ(got.tick,       sent.tick);
    EXPECT_EQ(got.moveX,      sent.moveX);
    EXPECT_EQ(got.moveZ,      sent.moveZ);
    EXPECT_EQ(got.yawDelta,   sent.yawDelta);
    EXPECT_EQ(got.pitchDelta, sent.pitchDelta);
    EXPECT_EQ(got.buttons,    sent.buttons);
    EXPECT_EQ(got.fireSerial, sent.fireSerial);
}

// ---------------------------------------------------------------------------
// toWire: quantization of float axes
// ---------------------------------------------------------------------------

TEST(InputMessageConvert, MoveXPlusOneGives127) {
    InputFrame frame{};
    frame.moveX = 1.0f;

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.moveX, static_cast<int8_t>(127));
}

TEST(InputMessageConvert, MoveXMinusOneGivesMinus127) {
    InputFrame frame{};
    frame.moveX = -1.0f;

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.moveX, static_cast<int8_t>(-127));
}

TEST(InputMessageConvert, MoveZZeroGivesZero) {
    InputFrame frame{};
    frame.moveZ = 0.0f;

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.moveZ, static_cast<int8_t>(0));
}

TEST(InputMessageConvert, MoveXClampedAboveOne) {
    InputFrame frame{};
    frame.moveX = 2.0f;  // out of [-1, 1] range

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.moveX, static_cast<int8_t>(127));
}

TEST(InputMessageConvert, YawDeltaQuantizedTo100ths) {
    InputFrame frame{};
    frame.lookYawDelta = 1.5f;  // 1.5 degrees => 150 in 1/100 units

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.yawDelta, static_cast<int16_t>(150));
}

TEST(InputMessageConvert, TickPassedThrough) {
    InputFrame frame{};
    const InputMessage msg = toWire(frame, 42u);
    EXPECT_EQ(msg.tick, 42u);
}

TEST(InputMessageConvert, ButtonBit0Jump) {
    InputFrame frame{};
    frame.digitalHeld = 0x01u;  // bit0 = jump

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.buttons & 0x01u, 0x01u) << "bit0 should be set for jump";
}

TEST(InputMessageConvert, ButtonBit1Fire) {
    InputFrame frame{};
    frame.digitalHeld = 0x02u;  // bit1 = fire

    const InputMessage msg = toWire(frame, 0u);
    EXPECT_EQ(msg.buttons & 0x02u, 0x02u) << "bit1 should be set for fire";
}

// ---------------------------------------------------------------------------
// Ring buffer: 4 clientTick calls → pendingInputCount_ == 4, oldest overwritten
// ---------------------------------------------------------------------------

// Minimal IGameMode stub
struct NullGameMode : engine::app::IGameMode {
    void onRoundStart(engine::app::GameContext&) override {}
    void onRoundTick(engine::app::GameContext&, float) override {}
    void onRoundEnd(engine::app::GameContext&) override {}
    void onMatchEnd(engine::app::GameContext&) override {}
    void onPlayerJoin(engine::app::GameContext&, uint32_t) override {}
    void onPlayerLeave(engine::app::GameContext&, uint32_t) override {}
    void onPlayerSpawn(engine::app::GameContext&, uint32_t) override {}
    void onPlayerDeath(engine::app::GameContext&, uint32_t, uint32_t) override {}
    void serializeState(engine::app::BitStreamWriter&) const override {}
    void deserializeState(engine::app::BitStreamReader&) override {}
    bool evaluateWinCondition(engine::app::GameContext&) override { return false; }
};

TEST(InputMessageRingBuffer, FourTicksFlushesAtMostThree) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};
    InputFrame frame{};

    // Tick 0: moveX = 0.1
    frame.moveX = 0.1f;
    loop.clientTick(ctx, 1.0f / 64.0f, frame);

    // Tick 1: moveX = 0.2
    frame.moveX = 0.2f;
    loop.clientTick(ctx, 1.0f / 64.0f, frame);

    // Tick 2: moveX = 0.3
    frame.moveX = 0.3f;
    loop.clientTick(ctx, 1.0f / 64.0f, frame);

    // Tick 3: moveX = 0.4 — should overwrite the slot used by tick 0
    frame.moveX = 0.4f;
    loop.clientTick(ctx, 1.0f / 64.0f, frame);

    // Flush: should write exactly 3 messages (ring size)
    std::array<uint8_t, 256> buf{};
    BitWriter writer(buf);
    loop.flushInputMessages(writer);

    ASSERT_FALSE(writer.overflow()) << "Flush into 256-byte buffer must not overflow";

    // 3 messages × (4+1+1+2+2+1+1 = 12 bytes = 96 bits) = 288 bits = 36 bytes
    // The exact bit count: each message is 96 bits
    EXPECT_EQ(writer.bitsWritten(), 3u * (32u + 8u + 8u + 16u + 16u + 8u + 8u))
        << "Should write exactly 3 messages worth of bits";
}

TEST(InputMessageRingBuffer, MostRecentMessageFirst) {
    GameLoop loop;
    loop.init({});

    GameContext ctx{};

    // Tick 0: moveX quantizes to ~12 (0.1 * 127 = 12.7 → 12)
    InputFrame f0{};
    f0.moveX = 0.1f;
    loop.clientTick(ctx, 1.0f / 64.0f, f0);

    // Tick 1: moveX quantizes to ~25 (0.2 * 127 = 25.4 → 25)
    InputFrame f1{};
    f1.moveX = 0.2f;
    loop.clientTick(ctx, 1.0f / 64.0f, f1);

    // Tick 2: moveX quantizes to ~38 (0.3 * 127 = 38.1 → 38)
    InputFrame f2{};
    f2.moveX = 0.3f;
    loop.clientTick(ctx, 1.0f / 64.0f, f2);

    std::array<uint8_t, 256> buf{};
    BitWriter writer(buf);
    loop.flushInputMessages(writer);

    // Read back — first message should be the most recent (tick 2, moveX ~38)
    BitReader reader(writer.writtenData());
    const InputMessage first = engine::networking::readInputMessage(reader);

    EXPECT_EQ(first.tick, 2u) << "First flushed message should have tick==2 (most recent)";
    // moveX for 0.3f: 0.3 * 127 = 38.1 → truncated to 38
    EXPECT_EQ(first.moveX, static_cast<int8_t>(38));
}
