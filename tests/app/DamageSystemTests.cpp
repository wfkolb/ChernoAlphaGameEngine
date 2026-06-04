#include <gtest/gtest.h>

#include "app/DamageSystem.h"

#include <core/EventBus.h>
#include <core/ecs/World.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>
#include <core/math/Vec.h>

#include <networking/HitscanValidationRequest.h>
#include <networking/UnreliableOrderedChannel.h>
#include <networking/WinsockGuard.h>
#include <string>
#include <networking/NetworkRegistry.h>
#include <networking/BitWriter.h>
#include <networking/BitReader.h>
#include <networking/ReplicatedComponentBit.h>

#include <physics/PhysicsWorld.h>
#include <physics/RigidBody.h>
#include <physics/ColliderShape.h>

#include <vector>

using engine::core::EventBus;
using engine::core::Health;
using engine::core::Transform;
using engine::core::ecs::Entity;
using engine::core::ecs::World;
using engine::core::ecs::kInvalidEntity;
using engine::core::math::Vec3;

using engine::networking::ByteReader;
using engine::networking::ByteWriter;
using engine::networking::HitscanValidationRequest;
using engine::networking::NetworkRegistry;
using engine::networking::UnreliableOrderedChannel;

using engine::app::DamageSystem;
using engine::app::EntityDiedEvent;
using engine::app::HitscanHitEvent;

// ── HitscanValidationRequest serialization ────────────────────────────────────

TEST(HitscanValidationRequest, RoundTrip) {
    HitscanValidationRequest req;
    req.fireSerial  = 0xDEADBEEFu;
    req.clientTick  = 4242u;
    req.origin      = {1.0f, 2.0f, 3.0f};
    req.direction   = {0.0f, 0.0f, 1.0f};
    req.targetNetId = 77u;

    ByteWriter bw;
    req.serialize(bw);
    EXPECT_EQ(bw.byteSize(), HitscanValidationRequest::kWireSize);

    ByteReader br(bw.data(), bw.byteSize());
    const HitscanValidationRequest out = HitscanValidationRequest::deserialize(br);
    EXPECT_TRUE(br.ok());
    EXPECT_EQ(out.fireSerial,  req.fireSerial);
    EXPECT_EQ(out.clientTick,  req.clientTick);
    EXPECT_EQ(out.targetNetId, req.targetNetId);
    EXPECT_FLOAT_EQ(out.origin.x, 1.0f);
    EXPECT_FLOAT_EQ(out.origin.z, 3.0f);
    EXPECT_FLOAT_EQ(out.direction.z, 1.0f);
}

// ── UnreliableOrderedChannel sequence logic ───────────────────────────────────

// Fixture owns a WinsockGuard so socket creation succeeds, and a bound receiver
// socket so sends in SendSequenceAdvances have a valid loopback destination.
struct ChannelFixture : ::testing::Test {
    engine::networking::WinsockGuard winsock;
    engine::networking::Socket       recvSock;  // valid destination for test sends

    void SetUp() override {
        recvSock = engine::networking::Socket::createUdp();
        recvSock.bind(0);
    }

    UnreliableOrderedChannel makeChannel() {
        auto sock = engine::networking::Socket::createUdp();
        sock.bind(0);
        // Route sends to the loopback receiver so sendto doesn't hit an error path.
        const uint16_t recvPort = recvSock.localEndpoint().port;
        engine::networking::Endpoint remote =
            engine::networking::Endpoint::fromString("[::1]:" + std::to_string(recvPort));
        return UnreliableOrderedChannel(std::move(sock), remote);
    }
};

TEST_F(ChannelFixture, FirstSequenceAccepted) {
    auto ch = makeChannel();
    EXPECT_TRUE(ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 0u));
}

TEST_F(ChannelFixture, NewerSequenceAccepted) {
    auto ch = makeChannel();
    EXPECT_TRUE (ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 5u));
    EXPECT_TRUE (ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 6u));
}

TEST_F(ChannelFixture, StaleAndDuplicateRejected) {
    auto ch = makeChannel();
    EXPECT_TRUE (ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 10u));
    EXPECT_FALSE(ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 9u));   // stale
    EXPECT_FALSE(ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 10u));  // dup
    EXPECT_TRUE (ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 11u));  // newer
}

TEST_F(ChannelFixture, KeysAreIndependent) {
    auto ch = makeChannel();
    EXPECT_TRUE(ch.acceptSequence(1u, engine::networking::RCB_HEALTH,       20u));
    // Different component bit on the same entity tracks its own sequence.
    EXPECT_TRUE(ch.acceptSequence(1u, engine::networking::RCB_WEAPON_STATE, 3u));
    // Different entity, same bit.
    EXPECT_TRUE(ch.acceptSequence(2u, engine::networking::RCB_HEALTH,       1u));
}

TEST_F(ChannelFixture, SequenceWrapAround) {
    auto ch = makeChannel();
    EXPECT_TRUE (ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 65530u));
    EXPECT_TRUE (ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 2u));    // wrapped, newer
    EXPECT_FALSE(ch.acceptSequence(1u, engine::networking::RCB_HEALTH, 65531u)); // pre-wrap, stale
}

TEST_F(ChannelFixture, SendSequenceAdvances) {
    auto ch = makeChannel();
    EXPECT_EQ(ch.peekSendSequence(1u, engine::networking::RCB_HEALTH), 0u);
    const uint8_t payload[2] = {0xAB, 0xCD};
    ch.send(1u, engine::networking::RCB_HEALTH, payload);
    EXPECT_EQ(ch.peekSendSequence(1u, engine::networking::RCB_HEALTH), 1u);
}

// ── DamageSystem ──────────────────────────────────────────────────────────────

struct DamageFixture : ::testing::Test {
    World                         world;
    engine::physics::PhysicsWorld physics;
    NetworkRegistry               registry;
    EventBus                      bus;

    // Spawn a damageable target: ECS entity + Health + Transform + a static
    // physics sphere at `pos`, registered under `netId`.
    Entity spawnTarget(uint32_t netId, Vec3 pos, float hp = 100.0f,
                       float shield = 0.0f) {
        Entity e = world.createEntity();
        Transform tr; tr.position = pos;
        world.addComponent<Transform>(e, tr);
        Health h; h.currentHp = hp; h.maxHp = 100.0f; h.shieldPercent = shield;
        world.addComponent<Health>(e, h);

        engine::physics::RigidBody rb; rb.type = engine::physics::RigidBodyType::Static;
        engine::physics::Collider  c;  c.shape = engine::physics::SphereShape{1.0f};
        engine::core::Transform pt; pt.position = pos;
        physics.addRigidBody(e, pt, rb, c);

        registry.registerEntity(netId, e);
        return e;
    }

    // A request firing from the origin straight down +Z toward a target on +Z.
    static HitscanValidationRequest shotAt(uint32_t targetNetId, uint32_t fireSerial) {
        HitscanValidationRequest req;
        req.fireSerial  = fireSerial;
        req.clientTick  = 1u;
        req.origin      = {0.0f, 0.0f, 0.0f};
        req.direction   = {0.0f, 0.0f, 1.0f};
        req.targetNetId = targetNetId;
        return req;
    }
};

TEST_F(DamageFixture, SingleHitAppliesDamage) {
    Entity target = spawnTarget(/*netId*/ 1u, {0, 0, 5});
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 30.0f);

    dmg.submitRequest(shotAt(1u, /*fireSerial*/ 100u));
    dmg.tick();

    EXPECT_FLOAT_EQ(world.get<Health>(target).currentHp, 70.0f);
}

TEST_F(DamageFixture, HitPublishesHitscanHitEvent) {
    spawnTarget(1u, {0, 0, 5});
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 30.0f);

    int hits = 0;
    bus.subscribe<HitscanHitEvent>([&](const HitscanHitEvent& e){
        ++hits;
        EXPECT_EQ(e.fireSerial, 100u);
        EXPECT_FLOAT_EQ(e.damage, 30.0f);
    });

    dmg.submitRequest(shotAt(1u, 100u));
    dmg.tick();
    EXPECT_EQ(hits, 1);
}

TEST_F(DamageFixture, MissAppliesNoDamage) {
    Entity target = spawnTarget(1u, {0, 0, 5});
    DamageSystem dmg(world, physics, registry, bus);

    // Fire away from the target.
    HitscanValidationRequest req = shotAt(1u, 100u);
    req.direction = {1.0f, 0.0f, 0.0f};
    dmg.submitRequest(req);
    dmg.tick();

    EXPECT_FLOAT_EQ(world.get<Health>(target).currentHp, 100.0f);
}

TEST_F(DamageFixture, ConcurrentHitsCommute) {
    Entity target = spawnTarget(1u, {0, 0, 5}, /*hp*/ 100.0f);
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 30.0f);
    dmg.setWeaponDamage(1u, 40.0f);

    // Two simultaneous hits in the same tick: 30 then 40 → 30 total HP.
    dmg.submitRequest(shotAt(1u, 100u), /*weaponType*/ 0u);
    dmg.submitRequest(shotAt(1u, 101u), /*weaponType*/ 1u);
    dmg.tick();

    EXPECT_FLOAT_EQ(world.get<Health>(target).currentHp, 30.0f);
}

TEST_F(DamageFixture, DamageClampsAtZero) {
    Entity target = spawnTarget(1u, {0, 0, 5}, /*hp*/ 20.0f);
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 999.0f);

    dmg.submitRequest(shotAt(1u, 100u));
    dmg.tick();

    EXPECT_FLOAT_EQ(world.get<Health>(target).currentHp, 0.0f);
}

TEST_F(DamageFixture, ShieldAbsorbsBeforeHp) {
    // 50% shield: a 40-damage hit deals 20 to HP.
    Entity target = spawnTarget(1u, {0, 0, 5}, /*hp*/ 100.0f, /*shield*/ 0.5f);
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 40.0f);

    dmg.submitRequest(shotAt(1u, 100u));
    dmg.tick();

    EXPECT_FLOAT_EQ(world.get<Health>(target).currentHp, 80.0f);
}

TEST_F(DamageFixture, DedupByFireSerial) {
    Entity target = spawnTarget(1u, {0, 0, 5}, /*hp*/ 100.0f);
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 30.0f);

    // Same fireSerial submitted twice across two ticks → applied once.
    dmg.submitRequest(shotAt(1u, 100u));
    dmg.tick();
    dmg.submitRequest(shotAt(1u, 100u));
    dmg.tick();

    EXPECT_FLOAT_EQ(world.get<Health>(target).currentHp, 70.0f);
}

TEST_F(DamageFixture, KillPublishesDeathEventAndQueuesRpc) {
    Entity target = spawnTarget(/*netId*/ 9u, {0, 0, 5}, /*hp*/ 20.0f);
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 50.0f);

    // Attribute the kill to an attacker netId.
    Entity attacker = world.createEntity();
    dmg.setAttackerForRequest(/*fireSerial*/ 100u, attacker, /*attackerNetId*/ 3u);

    int deaths = 0;
    bus.subscribe<EntityDiedEvent>([&](const EntityDiedEvent& e){
        ++deaths;
        EXPECT_EQ(e.dead, target);
        EXPECT_EQ(e.killer, attacker);
    });

    dmg.submitRequest(shotAt(9u, 100u));
    dmg.tick();

    EXPECT_EQ(deaths, 1);
    ASSERT_EQ(dmg.pendingDeathRpcs().size(), 1u);
    EXPECT_EQ(dmg.pendingDeathRpcs()[0].deadNetId,   9u);
    EXPECT_EQ(dmg.pendingDeathRpcs()[0].killerNetId, 3u);

    dmg.clearPendingDeathRpcs();
    EXPECT_TRUE(dmg.pendingDeathRpcs().empty());
}

TEST_F(DamageFixture, NoDeathRpcWhenSurviving) {
    spawnTarget(1u, {0, 0, 5}, /*hp*/ 100.0f);
    DamageSystem dmg(world, physics, registry, bus);
    dmg.setWeaponDamage(0u, 10.0f);

    dmg.submitRequest(shotAt(1u, 100u));
    dmg.tick();

    EXPECT_TRUE(dmg.pendingDeathRpcs().empty());
}

TEST(DamageSystemRpc, PlayerDiedDescriptorIsReliableToAllClients) {
    using engine::networking::RpcReliability;
    using engine::networking::RpcTarget;
    EXPECT_EQ(DamageSystem::kPlayerDiedRpc.reliability, RpcReliability::Reliable);
    EXPECT_EQ(DamageSystem::kPlayerDiedRpc.target,      RpcTarget::AllClients);
}
