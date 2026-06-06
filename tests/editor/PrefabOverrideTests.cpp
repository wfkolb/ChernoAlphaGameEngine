#include <gtest/gtest.h>

#include <core/ecs/PrefabInstance.h>
#include <core/components/Health.h>
#include <tools/PrefabSerializer.h>

#include <cstring>

using engine::core::Health;
using engine::core::ecs::PrefabInstance;
using engine::tools::PrefabSerializer;

// ── helpers that mirror the editor's override-detection logic ─────────────────

static bool detectOverride(const void* liveBytes, const void* prefabBytes, size_t size) {
    return std::memcmp(liveBytes, prefabBytes, size) != 0;
}

static void markOverridden(PrefabInstance& inst, uint8_t bit) {
    inst.overriddenComponents |= (1u << bit);
}

static void clearOverride(PrefabInstance& inst, uint8_t bit) {
    inst.overriddenComponents &= ~(1u << bit);
}

static bool isOverridden(const PrefabInstance& inst, uint8_t bit) {
    return (inst.overriddenComponents >> bit) & 1u;
}

// ── detectOverride ────────────────────────────────────────────────────────────

TEST(PrefabOverrideTest, IdenticalDataIsNotOverridden) {
    Health a{};
    Health b{};
    EXPECT_FALSE(detectOverride(&a, &b, sizeof(Health)));
}

TEST(PrefabOverrideTest, ModifiedFieldIsDetectedAsOverride) {
    Health prefab{};
    prefab.currentHp = 100.f;

    Health live{};
    live.currentHp = 50.f;   // player took damage — value diverges from prefab

    EXPECT_TRUE(detectOverride(&live, &prefab, sizeof(Health)));
}

TEST(PrefabOverrideTest, RestoringBytesEliminatesOverride) {
    Health prefab{};
    prefab.currentHp = 100.f;
    prefab.maxHp     = 100.f;

    Health live = prefab;
    live.currentHp = 25.f;
    ASSERT_TRUE(detectOverride(&live, &prefab, sizeof(Health)));

    // Revert live back to prefab bytes.
    std::memcpy(&live, &prefab, sizeof(Health));
    EXPECT_FALSE(detectOverride(&live, &prefab, sizeof(Health)));
}

// ── overriddenComponents bitmask ──────────────────────────────────────────────

TEST(PrefabOverrideTest, MarkSetsBit) {
    PrefabInstance inst{};
    EXPECT_FALSE(isOverridden(inst, 3u));
    markOverridden(inst, 3u);
    EXPECT_TRUE(isOverridden(inst, 3u));
}

TEST(PrefabOverrideTest, ClearUnsetsBit) {
    PrefabInstance inst{};
    markOverridden(inst, 5u);
    EXPECT_TRUE(isOverridden(inst, 5u));
    clearOverride(inst, 5u);
    EXPECT_FALSE(isOverridden(inst, 5u));
}

TEST(PrefabOverrideTest, MultipleBitsAreIndependent) {
    PrefabInstance inst{};
    markOverridden(inst, 0u);
    markOverridden(inst, 7u);
    markOverridden(inst, 31u);

    EXPECT_TRUE(isOverridden(inst, 0u));
    EXPECT_TRUE(isOverridden(inst, 7u));
    EXPECT_TRUE(isOverridden(inst, 31u));
    EXPECT_FALSE(isOverridden(inst, 1u));
    EXPECT_FALSE(isOverridden(inst, 15u));

    clearOverride(inst, 7u);
    EXPECT_TRUE(isOverridden(inst, 0u));
    EXPECT_FALSE(isOverridden(inst, 7u));
    EXPECT_TRUE(isOverridden(inst, 31u));
}

TEST(PrefabOverrideTest, ClearOnUnsetBitIsNoOp) {
    PrefabInstance inst{};
    clearOverride(inst, 2u);
    EXPECT_EQ(inst.overriddenComponents, 0u);
}

// ── round-trip with PrefabData component bytes ────────────────────────────────

TEST(PrefabOverrideTest, PrefabDataBytesMatchLiveComponentAfterCapture) {
    // Build a minimal PrefabData by hand to avoid World/registration overhead.
    PrefabSerializer::ComponentData cd;
    cd.typeId = Health::kComponentId;
    cd.bytes.resize(sizeof(Health));

    Health h{};
    h.currentHp = 80.f;
    h.maxHp     = 100.f;
    std::memcpy(cd.bytes.data(), &h, sizeof(Health));

    // Live data starts identical — no override.
    Health live{};
    std::memcpy(&live, cd.bytes.data(), sizeof(Health));
    EXPECT_FALSE(detectOverride(&live, cd.bytes.data(), sizeof(Health)));

    // Modify live → override detected.
    live.currentHp = 10.f;
    EXPECT_TRUE(detectOverride(&live, cd.bytes.data(), sizeof(Health)));

    // Revert from prefab bytes → no override again.
    std::memcpy(&live, cd.bytes.data(), sizeof(Health));
    EXPECT_FALSE(detectOverride(&live, cd.bytes.data(), sizeof(Health)));
    EXPECT_FLOAT_EQ(live.currentHp, 80.f);
}
