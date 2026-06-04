#include <gtest/gtest.h>
#include <networking/NetworkIdAllocator.h>
#include <networking/NetworkRegistry.h>
#include <networking/NetworkIdentity.h>
#include <networking/ReplicatedComponentBit.h>
#include <networking/ReplicationSystem.h>
#include <core/ecs/World.h>
#include <core/ecs/Entity.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>

using namespace engine::networking;
using namespace engine::core;
using namespace engine::core::ecs;

// ── NetworkIdAllocator ─────────────────────────────────────────────────────────

TEST(NetworkIdAllocator, AllocatesSequentially) {
    NetworkIdAllocator alloc;
    EXPECT_EQ(alloc.allocate(), 1u);
    EXPECT_EQ(alloc.allocate(), 2u);
    EXPECT_EQ(alloc.allocate(), 3u);
}

TEST(NetworkIdAllocator, ZeroIsInvalid) {
    EXPECT_EQ(NetworkIdAllocator::kInvalid, 0u);
}

TEST(NetworkIdAllocator, RecyclesReleasedIdLIFO) {
    NetworkIdAllocator alloc;
    const uint32_t a = alloc.allocate(); // 1
    const uint32_t b = alloc.allocate(); // 2
    alloc.release(a);
    alloc.release(b);
    EXPECT_EQ(alloc.allocate(), b); // LIFO: b comes back first
    EXPECT_EQ(alloc.allocate(), a);
}

TEST(NetworkIdAllocator, IgnoresReleaseOfInvalidId) {
    NetworkIdAllocator alloc;
    alloc.release(NetworkIdAllocator::kInvalid);
    EXPECT_EQ(alloc.allocate(), 1u); // no garbage in free list
}

TEST(NetworkIdAllocator, ResetRestartsMono) {
    NetworkIdAllocator alloc;
    alloc.allocate();
    alloc.allocate();
    alloc.reset();
    EXPECT_EQ(alloc.allocate(), 1u);
    EXPECT_EQ(alloc.nextMonotonic(), 2u);
}

// ── NetworkRegistry ────────────────────────────────────────────────────────────

TEST(NetworkRegistry, RegisterAndFind) {
    NetworkRegistry reg;
    Entity e{ 42u, 1u };
    reg.registerEntity(10u, e);
    EXPECT_TRUE(reg.contains(10u));
    EXPECT_EQ(reg.find(10u), e);
}

TEST(NetworkRegistry, FindUnknownReturnsInvalid) {
    NetworkRegistry reg;
    EXPECT_EQ(reg.find(99u), kInvalidEntity);
    EXPECT_FALSE(reg.contains(99u));
}

TEST(NetworkRegistry, UnregisterRemovesEntry) {
    NetworkRegistry reg;
    Entity e{ 1u, 0u };
    reg.registerEntity(5u, e);
    reg.unregisterEntity(5u);
    EXPECT_FALSE(reg.contains(5u));
    EXPECT_EQ(reg.size(), 0u);
}

TEST(NetworkRegistry, ClearRemovesAll) {
    NetworkRegistry reg;
    reg.registerEntity(1u, Entity{ 0u, 0u });
    reg.registerEntity(2u, Entity{ 1u, 0u });
    reg.clear();
    EXPECT_EQ(reg.size(), 0u);
}

TEST(NetworkRegistry, OverwriteExistingId) {
    NetworkRegistry reg;
    Entity a{ 1u, 0u };
    Entity b{ 2u, 0u };
    reg.registerEntity(7u, a);
    reg.registerEntity(7u, b);
    EXPECT_EQ(reg.find(7u), b);
    EXPECT_EQ(reg.size(), 1u);
}

// ── ReplicationSystem ─────────────────────────────────────────────────────────

// Register the components the replication system inspects.
// World::registerComponent uses a static counter so we do this once per binary.
struct ReplicationFixture : ::testing::Test {
    static void SetUpTestSuite() {
        World::registerComponent<NetworkIdentity>({
            "NetworkIdentity", sizeof(NetworkIdentity), alignof(NetworkIdentity),
            [](void* p){ new(p) NetworkIdentity{}; }, nullptr, nullptr
        });
        World::registerComponent<Transform>({
            "Transform", sizeof(Transform), alignof(Transform),
            [](void* p){ new(p) Transform{}; }, nullptr, nullptr
        });
        World::registerComponent<Health>({
            "Health", sizeof(Health), alignof(Health),
            [](void* p){ new(p) Health{}; }, nullptr, nullptr
        });
    }

    World             world;
    ReplicationSystem rep;

    void SetUp() override { rep.init(); }
};

TEST_F(ReplicationFixture, NoSnapshotBeforeInterval) {
    // At 20 Hz each snapshot requires 1/20 = 0.05 s. A 0.04 s tick is not enough.
    bool produced = rep.tick(world, 0.04f);
    EXPECT_FALSE(produced);
    EXPECT_FALSE(rep.hasSnapshot());
}

TEST_F(ReplicationFixture, SnapshotProducedAfterInterval) {
    bool produced = rep.tick(world, 0.051f);
    EXPECT_TRUE(produced);
    EXPECT_TRUE(rep.hasSnapshot());
}

TEST_F(ReplicationFixture, SequenceIncrementsEachSnapshot) {
    rep.tick(world, 0.051f);
    EXPECT_EQ(rep.currentSequence(), 1u);
    rep.tick(world, 0.051f);
    EXPECT_EQ(rep.currentSequence(), 2u);
}

TEST_F(ReplicationFixture, HeaderDecodesCorrectly) {
    rep.tick(world, 0.051f);

    ReplicationSystem::SnapshotHeader hdr;
    ASSERT_TRUE(rep.decodeHeader(hdr));
    EXPECT_EQ(hdr.seq, 1u);
    EXPECT_EQ(hdr.entityCount, 0u); // empty world
    EXPECT_EQ(hdr.ackedSeq, 0u);
}

TEST_F(ReplicationFixture, EntityCountReflectsWorld) {
    // Spawn two entities with NetworkIdentity.
    for (int i = 0; i < 2; ++i) {
        Entity e = world.createEntity();
        NetworkIdentity ni;
        ni.netId = static_cast<uint32_t>(i + 1);
        ni.replicatedComponents = 0u;
        world.addComponent<NetworkIdentity>(e, ni);
    }

    rep.tick(world, 0.051f);

    ReplicationSystem::SnapshotHeader hdr;
    ASSERT_TRUE(rep.decodeHeader(hdr));
    EXPECT_EQ(hdr.entityCount, 2u);
}

TEST_F(ReplicationFixture, AcknowledgeUpdatesAckedSeq) {
    rep.tick(world, 0.051f); // seq=1
    rep.acknowledgeSnapshot(1u);
    rep.tick(world, 0.051f); // seq=2

    ReplicationSystem::SnapshotHeader hdr;
    ASSERT_TRUE(rep.decodeHeader(hdr));
    EXPECT_EQ(hdr.ackedSeq, 1u);
}

TEST_F(ReplicationFixture, SnapshotMinimum19Bytes) {
    rep.tick(world, 0.051f);
    EXPECT_GE(rep.latestData().size(), 19u);
}
