// tests/app/SpawnExclusionTests.cpp
// Unit tests for N3 — SpawnPoint radius exclusion in GameLoop::onPlayerJoin.

#include <gtest/gtest.h>

#include "app/GameLoop.h"
#include "app/IGameMode.h"
#include "app/BitStream.h"
#include "app/GameContext.h"

#include <core/ecs/World.h>
#include <core/ecs/View.h>
#include <core/components/Transform.h>
#include <core/components/SpawnPointComponent.h>
#include <core/input/InputReceiverComponent.h>

#include <span>
#include <vector>

using namespace engine::app;
using engine::core::ecs::Entity;
using engine::core::ecs::kInvalidEntity;
using engine::core::ecs::World;
using engine::core::Transform;
using engine::core::SpawnPointComponent;
using engine::core::input::InputReceiverComponent;
using engine::core::math::Vec3;

// ── Helpers ───────────────────────────────────────────────────────────────────

// NullGameMode: satisfies IGameMode but does nothing except capture spawn candidates.
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

// CapturingGameMode: captures the candidates span passed to selectSpawnPoint.
struct CapturingGameMode : NullGameMode {
    std::vector<Entity> capturedCandidates;

    Entity selectSpawnPoint(
        uint32_t /*playerId*/,
        uint8_t  /*teamId*/,
        std::span<const Entity> availableSpawns) override
    {
        capturedCandidates.assign(availableSpawns.begin(), availableSpawns.end());
        return availableSpawns.empty() ? kInvalidEntity : availableSpawns[0];
    }
};

// Build a spawn point entity at a given position with given radius, teamId=0.
static Entity makeSpawnPoint(World& world, Vec3 pos, float radius)
{
    Entity e = world.createEntity();
    Transform tr{};
    tr.position = pos;
    world.addComponent<Transform>(e, tr);
    SpawnPointComponent sp{};
    sp.teamId   = 0;
    sp.priority = 0;
    sp.radius   = radius;
    world.addComponent<SpawnPointComponent>(e, sp);
    return e;
}

// Build a player entity with InputReceiverComponent at a given position.
static Entity makePlayer(World& world, Vec3 pos, uint8_t playerId)
{
    Entity e = world.createEntity();
    Transform tr{};
    tr.position = pos;
    world.addComponent<Transform>(e, tr);
    InputReceiverComponent recv{};
    recv.playerId = playerId;
    world.addComponent<InputReceiverComponent>(e, recv);
    return e;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// One player at spawnA — spawnA excluded, spawnB kept.
TEST(SpawnExclusion, PlayerAtSpawnA_ExcludesA_KeepsB)
{
    World world;

    // spawnA at (0,0,0) with radius=2.0f
    Entity spawnA = makeSpawnPoint(world, {0.0f, 0.0f, 0.0f}, 2.0f);
    // spawnB at (5,0,0) with radius=2.0f  — 5 m away, outside radius
    Entity spawnB = makeSpawnPoint(world, {5.0f, 0.0f, 0.0f}, 2.0f);

    // Player at spawnA's position
    makePlayer(world, {0.0f, 0.0f, 0.0f}, 1u);

    CapturingGameMode mode;
    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    ctx.world = &world;

    // Player 2 joins — their entity doesn't exist yet but that's fine;
    // onPlayerJoin looks for playerId=2 to teleport, won't find it — still calls
    // selectSpawnPoint with the filtered candidates.
    loop.onPlayerJoin(ctx, 2u, 0u);

    ASSERT_EQ(mode.capturedCandidates.size(), 1u)
        << "Only spawnB should be in the candidates (spawnA excluded)";
    EXPECT_EQ(mode.capturedCandidates[0], spawnB)
        << "The surviving candidate should be spawnB";
    // spawnA must not be in the list
    for (Entity e : mode.capturedCandidates) {
        EXPECT_NE(e, spawnA) << "spawnA should have been excluded";
    }
}

// Radius=0 disables exclusion — both spawns should appear even with player present.
TEST(SpawnExclusion, ZeroRadius_NoExclusion)
{
    World world;

    Entity spawnA = makeSpawnPoint(world, {0.0f, 0.0f, 0.0f}, 0.0f);
    Entity spawnB = makeSpawnPoint(world, {5.0f, 0.0f, 0.0f}, 0.0f);

    makePlayer(world, {0.0f, 0.0f, 0.0f}, 1u);

    CapturingGameMode mode;
    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    ctx.world = &world;

    loop.onPlayerJoin(ctx, 2u, 0u);

    EXPECT_EQ(mode.capturedCandidates.size(), 2u)
        << "Both spawns should remain when radius is 0";
}

// No players present — no exclusion, both spawns available.
TEST(SpawnExclusion, NoPlayers_BothSpawnsAvailable)
{
    World world;

    makeSpawnPoint(world, {0.0f, 0.0f, 0.0f}, 2.0f);
    makeSpawnPoint(world, {5.0f, 0.0f, 0.0f}, 2.0f);

    CapturingGameMode mode;
    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    ctx.world = &world;

    loop.onPlayerJoin(ctx, 1u, 0u);

    EXPECT_EQ(mode.capturedCandidates.size(), 2u)
        << "With no existing players both spawns should be available";
}

// Fallback: both spawns excluded — full list should be restored (2 candidates).
TEST(SpawnExclusion, AllExcluded_FallbackRestoresFullList)
{
    World world;

    Entity spawnA = makeSpawnPoint(world, {0.0f, 0.0f, 0.0f}, 2.0f);
    Entity spawnB = makeSpawnPoint(world, {5.0f, 0.0f, 0.0f}, 2.0f);

    // Place a player at each spawn point — both become occupied.
    makePlayer(world, {0.0f, 0.0f, 0.0f}, 1u);
    makePlayer(world, {5.0f, 0.0f, 0.0f}, 2u);

    CapturingGameMode mode;
    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    ctx.world = &world;

    loop.onPlayerJoin(ctx, 3u, 0u);

    // Both spawnA and spawnB excluded → fallback keeps original 2-candidate list.
    ASSERT_EQ(mode.capturedCandidates.size(), 2u)
        << "Fallback should restore both candidates when all are excluded";

    bool hasA = false, hasB = false;
    for (Entity e : mode.capturedCandidates) {
        if (e == spawnA) hasA = true;
        if (e == spawnB) hasB = true;
    }
    EXPECT_TRUE(hasA) << "spawnA should be in the fallback list";
    EXPECT_TRUE(hasB) << "spawnB should be in the fallback list";
}

// Player exactly on the radius boundary (distance == radius) is treated as occupied.
TEST(SpawnExclusion, PlayerOnRadiusBoundary_Excluded)
{
    World world;

    // spawnA at (0,0,0) with radius=3.0f, player at (3,0,0) — exactly on boundary.
    Entity spawnA = makeSpawnPoint(world, {0.0f, 0.0f, 0.0f}, 3.0f);
    Entity spawnB = makeSpawnPoint(world, {10.0f, 0.0f, 0.0f}, 3.0f);

    makePlayer(world, {3.0f, 0.0f, 0.0f}, 1u);

    CapturingGameMode mode;
    GameLoop loop;
    loop.init({ nullptr, nullptr, &mode });

    GameContext ctx{};
    ctx.world = &world;

    loop.onPlayerJoin(ctx, 2u, 0u);

    ASSERT_EQ(mode.capturedCandidates.size(), 1u);
    EXPECT_EQ(mode.capturedCandidates[0], spawnB)
        << "spawnA on the boundary should be excluded; only spawnB survives";
}
