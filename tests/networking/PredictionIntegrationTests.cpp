// PredictionIntegrationTests.cpp
//
// Integration test for client prediction and server reconciliation.
//
// This test exercises the prediction/reconciliation math directly using
// PredictionBuffer — the same logic the networked client uses, without
// requiring the full session machinery.  The strategy mirrors the real
// flow: client predicts each tick and records its estimate in a
// PredictionBuffer; at tick 5 the server injects a 2-tick divergence;
// when the client receives the authoritative position it calls
// PredictionBuffer::reconcile(), detects the error, and snaps to the
// server state.

#include <gtest/gtest.h>
#include <networking/PredictionBuffer.h>
#include <core/components/Transform.h>
#include <core/input/InputFrame.h>
#include <core/math/Vec.h>

using namespace engine::networking;
using namespace engine::core;
using namespace engine::core::input;
using namespace engine::core::math;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Movement step applied every tick (1 m/s @ 64 Hz ≈ 0.015625 m per tick).
static constexpr float kTickDeltaX = 1.0f / 64.0f;

// The divergence injected on the server at tick 5 (4 ticks worth of position).
// Must exceed kReconcileThreshold (0.05 m); 4 * (1/64) = 0.0625 m > 0.05 m.
static constexpr float kDivergenceDelta = 4.0f * kTickDeltaX;

// Apply the canonical movement update for one tick.
Transform applyMovement(const Transform& prev) {
    Transform next = prev;
    next.position.x += kTickDeltaX;
    return next;
}

// Build a minimal InputFrame for a given tick.
InputFrame makeInputFrame(uint32_t tick) {
    InputFrame f{};
    f.tick = tick;
    f.moveX = 1.0f; // strafe right
    return f;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(PredictionIntegration, DivergenceDetectedAndReconciled) {
    // ── Setup ────────────────────────────────────────────────────────────────
    // Server and client both start at the origin.
    Transform serverPos{};
    Transform clientPos{};
    PredictionBuffer predBuf;

    // ── Simulate 10 ticks ───────────────────────────────────────────────────
    for (uint32_t tick = 0; tick < 10u; ++tick) {
        // --- Server tick ---
        serverPos = applyMovement(serverPos);

        // At tick 5 (index, 0-based), inject a 2-tick divergence on the server.
        if (tick == 5u) {
            serverPos.position.x += kDivergenceDelta;
        }

        // --- Client tick ---
        // Client predicts using the same movement logic (no knowledge of the
        // server divergence yet).
        InputFrame frame = makeInputFrame(tick);
        clientPos = applyMovement(clientPos);
        predBuf.push(frame, clientPos);
    }

    // ── Reconcile at tick 5 (the diverging tick) ────────────────────────────
    // Re-derive what the server position was at tick 5 so we can send it.
    // serverPos after tick-5 movement + divergence:
    //   After 6 ticks of movement + kDivergenceDelta injected at tick 5.
    Transform serverAtTick5{};
    for (uint32_t t = 0; t <= 5u; ++t) {
        serverAtTick5 = applyMovement(serverAtTick5);
    }
    serverAtTick5.position.x += kDivergenceDelta;

    // Client predicted position at tick 5 (no divergence applied):
    //   6 ticks of movement = 6 * kTickDeltaX
    //   Divergence = 2 * kTickDeltaX → error = kDivergenceDelta
    const float divergenceM = distance(
        serverAtTick5.position,
        predBuf.get(5u) != nullptr ? predBuf.get(5u)->predicted.position : Vec3::zero()
    );

    // Divergence must be >= threshold to trigger reconciliation.
    EXPECT_GE(divergenceM, PredictionBuffer::kReconcileThreshold)
        << "Injected divergence (" << divergenceM
        << " m) should be >= kReconcileThreshold ("
        << PredictionBuffer::kReconcileThreshold << " m)";

    // reconcile() must confirm the mismatch.
    Transform reconciledPos{};
    const bool needsReconcile = predBuf.reconcile(5u, serverAtTick5, &reconciledPos);
    EXPECT_TRUE(needsReconcile)
        << "reconcile() should return true when divergence exceeds threshold";

    // ── Re-simulate ticks 6–9 from the reconciled position ──────────────────
    // In real code the client replays all buffered inputs from tick 6 onward.
    // Here we simply advance 4 more ticks from the authoritative base.
    Transform resimPos = reconciledPos; // authoritative position at end of tick 5
    for (uint32_t tick = 6u; tick < 10u; ++tick) {
        resimPos = applyMovement(resimPos);
    }

    // ── Verify final positions match within 1 mm ────────────────────────────
    const float finalDiff = distance(resimPos.position, serverPos.position);
    EXPECT_NEAR(finalDiff, 0.0f, 0.001f)
        << "After re-simulation, client position should match server within 1 mm. "
        << "client.x=" << resimPos.position.x
        << " server.x=" << serverPos.position.x;
}

TEST(PredictionIntegration, NoDivergenceBelowThreshold) {
    // Verify that small floating-point noise below 5 cm does NOT trigger
    // reconciliation (regression guard for false-positive re-simulations).
    PredictionBuffer predBuf;
    Transform predicted{};
    InputFrame f = makeInputFrame(0u);
    predBuf.push(f, predicted);

    // Server position deviates by only 1 cm — below the 5 cm threshold.
    Transform server{};
    server.position.x = 0.01f;

    Transform out{};
    EXPECT_FALSE(predBuf.reconcile(0u, server, &out))
        << "reconcile() must return false for sub-threshold divergence";
}

TEST(PredictionIntegration, ReconcileSnapsToAuthoritative) {
    // When reconciliation fires, *outServer must equal the server transform.
    PredictionBuffer predBuf;
    Transform predicted{};
    predicted.position.x = 0.0f;
    InputFrame f = makeInputFrame(7u);
    predBuf.push(f, predicted);

    Transform server{};
    server.position.x = 10.0f; // large divergence

    Transform out{};
    const bool fired = predBuf.reconcile(7u, server, &out);
    ASSERT_TRUE(fired);
    EXPECT_FLOAT_EQ(out.position.x, server.position.x)
        << "After reconciliation, outServer.x must equal the authoritative value";
}
