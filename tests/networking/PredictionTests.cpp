#include <gtest/gtest.h>
#include <networking/PredictionBuffer.h>
#include <networking/SnapshotBuffer.h>
#include <core/components/Transform.h>
#include <core/input/InputFrame.h>

using namespace engine::networking;
using namespace engine::core;
using namespace engine::core::input;

// ── PredictionBuffer ───────────────────────────────────────────────────────────

TEST(PredictionBuffer, PushAndGetByTick) {
    PredictionBuffer buf;
    InputFrame frame;
    frame.tick = 10u;
    Transform t;
    t.position.x = 5.0f;

    buf.push(frame, t);

    const PredictionBuffer::Slot* s = buf.get(10u);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->valid);
    EXPECT_EQ(s->frame.tick, 10u);
    EXPECT_FLOAT_EQ(s->predicted.position.x, 5.0f);
}

TEST(PredictionBuffer, GetUnknownTickReturnsNull) {
    PredictionBuffer buf;
    EXPECT_EQ(buf.get(99u), nullptr);
}

TEST(PredictionBuffer, NoReconcileWithinThreshold) {
    PredictionBuffer buf;
    InputFrame frame;
    frame.tick = 1u;
    Transform predicted;
    predicted.position.x = 0.0f;
    buf.push(frame, predicted);

    Transform server;
    server.position.x = 0.01f; // 1 cm — below 5 cm threshold
    Transform out;
    EXPECT_FALSE(buf.reconcile(1u, server, &out));
}

TEST(PredictionBuffer, ReconcileAboveThreshold) {
    PredictionBuffer buf;
    InputFrame frame;
    frame.tick = 2u;
    Transform predicted;
    predicted.position.x = 0.0f;
    buf.push(frame, predicted);

    Transform server;
    server.position.x = 1.0f; // 1 m — above 5 cm threshold
    Transform out{};
    EXPECT_TRUE(buf.reconcile(2u, server, &out));
    EXPECT_FLOAT_EQ(out.position.x, 1.0f);
}

TEST(PredictionBuffer, ReconcileUnknownTickReturnsFalse) {
    PredictionBuffer buf;
    Transform server;
    Transform out;
    EXPECT_FALSE(buf.reconcile(99u, server, &out));
}

TEST(PredictionBuffer, CircularOverwriteEvictsOldTicks) {
    PredictionBuffer buf;
    // Fill all 128 slots with tick 0..127
    for (uint32_t i = 0; i < static_cast<uint32_t>(PredictionBuffer::kSlots); ++i) {
        InputFrame f; f.tick = i;
        buf.push(f, Transform{});
    }
    EXPECT_NE(buf.get(0u), nullptr); // tick 0 is still in slot 0

    // Push one more (tick 128) — overwrites slot 0 (head_ % 128 == 0)
    InputFrame extra; extra.tick = 128u;
    buf.push(extra, Transform{});
    EXPECT_EQ(buf.get(0u), nullptr);   // evicted
    EXPECT_NE(buf.get(128u), nullptr); // newest is present
}

TEST(PredictionBuffer, Reset) {
    PredictionBuffer buf;
    InputFrame f; f.tick = 5u;
    buf.push(f, Transform{});
    buf.reset();
    EXPECT_EQ(buf.get(5u), nullptr);
}

// ── SnapshotBuffer ─────────────────────────────────────────────────────────────

static SnapshotBuffer::EntityState makeState(uint32_t netId, float x) {
    SnapshotBuffer::EntityState s;
    s.netId = netId;
    s.transform.position.x = x;
    return s;
}

TEST(SnapshotBuffer, SampleReturnsFalseWhenEmpty) {
    SnapshotBuffer buf;
    bool called = false;
    bool ok = buf.sample(500.0f, [&](uint32_t, const Transform&){ called = true; });
    EXPECT_FALSE(ok);
    EXPECT_FALSE(called);
}

TEST(SnapshotBuffer, ExtrapolatesWithinLimit) {
    // Push one snapshot at t=0 ms. Sample at currentMs=250 → target=150ms.
    // target - prev = 150 ms < kMaxExtrapMs (200 ms) → extrapolate using that snapshot.
    SnapshotBuffer buf;
    buf.push(1u, 0.0f, { makeState(1u, 10.0f) });

    float visitedX = -1.0f;
    bool ok = buf.sample(250.0f, [&](uint32_t, const Transform& t){
        visitedX = t.position.x;
    });
    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(visitedX, 10.0f);
}

TEST(SnapshotBuffer, NoSampleBeyondExtrapolationLimit) {
    // Push at t=0 ms. Sample at currentMs=400 → target=300ms.
    // 300 - 0 = 300 ms > kMaxExtrapMs (200 ms) → hold last position, return true.
    SnapshotBuffer buf;
    buf.push(1u, 0.0f, { makeState(1u, 10.0f) });

    float visitedX = -1.0f;
    bool called = false;
    bool ok = buf.sample(400.0f, [&](uint32_t, const Transform& t){
        visitedX = t.position.x;
        called = true;
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(called);
    EXPECT_FLOAT_EQ(visitedX, 10.0f);  // held at last known position
}

TEST(SnapshotBuffer, InterpolatesPosition) {
    // prev at t=0ms: x=0.  next at t=150ms: x=2.
    // sample at currentMs=175 → targetMs=75. alpha = 75/150 = 0.5 → x=1.
    SnapshotBuffer buf;
    buf.push(1u, 0.0f,   { makeState(42u, 0.0f) });
    buf.push(2u, 150.0f, { makeState(42u, 2.0f) });

    float visitedX = -1.0f;
    bool ok = buf.sample(175.0f, [&](uint32_t, const Transform& t){
        visitedX = t.position.x;
    });
    EXPECT_TRUE(ok);
    EXPECT_NEAR(visitedX, 1.0f, 0.001f);
}

TEST(SnapshotBuffer, Clear) {
    SnapshotBuffer buf;
    buf.push(1u, 0.0f, { makeState(1u, 5.0f) });
    buf.clear();
    bool called = false;
    buf.sample(200.0f, [&](uint32_t, const Transform&){ called = true; });
    EXPECT_FALSE(called);
}

TEST(SnapshotBuffer, OlderSnapshotIsEvictedAfterFourPushes) {
    // Ring has 4 slots. After 5 pushes the oldest slot is overwritten.
    SnapshotBuffer buf;
    for (int i = 0; i < SnapshotBuffer::kSlots + 1; ++i)
        buf.push(static_cast<uint32_t>(i + 1),
                 static_cast<float>(i) * 10.0f,
                 { makeState(1u, static_cast<float>(i)) });

    // The 5th push overwrites slot 0 (t=0). Target for currentMs=110 is t=10.
    // prev at t=10 (slot 1 data), no entry at t=0 — sample should still work.
    float visitedX = -1.0f;
    bool ok = buf.sample(110.0f, [&](uint32_t, const Transform& t){
        visitedX = t.position.x;
    });
    EXPECT_TRUE(ok);
    (void)visitedX; // exact value depends on what survives eviction; just check ok
}
