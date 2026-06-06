#include "app/DamageSystem.h"

#include <core/EventBus.h>
#include <core/components/Transform.h>
#include <core/log.h>
#include <physics/LagCompensator.h>
#include <physics/PhysicsWorld.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine::app {

using engine::core::ecs::Entity;
using engine::core::ecs::kInvalidEntity;
using engine::core::math::Vec3;

DamageSystem::DamageSystem(engine::core::ecs::World&              world,
                           engine::physics::PhysicsWorld&         physics,
                           engine::networking::NetworkRegistry&   registry,
                           engine::core::EventBus&                eventBus,
                           engine::networking::ReplicationSystem* replicationSystem)
    : world_(world)
    , physics_(physics)
    , registry_(registry)
    , eventBus_(eventBus)
    , replication_(replicationSystem)
    , lagComp_(physics)
{}

void DamageSystem::setWeaponDamage(uint8_t weaponType, float damage) {
    weaponDamage_[weaponType] = damage;
}

float DamageSystem::weaponDamage(uint8_t weaponType) const {
    const auto it = weaponDamage_.find(weaponType);
    return it == weaponDamage_.end() ? 25.0f : it->second;  // default rifle damage
}

void DamageSystem::setAttackerForRequest(uint32_t fireSerial, Entity attacker,
                                         uint32_t attackerNetId) {
    attackers_[fireSerial] = AttackerInfo{attacker, attackerNetId};
}

void DamageSystem::submitRequest(
    const engine::networking::HitscanValidationRequest& req,
    uint8_t weaponType) {
    queue_.push_back({req, weaponType});
}

engine::physics::RaycastHit DamageSystem::lagCompRaycast(
    uint32_t                        tick,
    const engine::core::math::Vec3& origin,
    const engine::core::math::Vec3& dir,
    float                           maxDist)
{
    engine::physics::RaycastHit invalid;
    invalid.hasHit = false;

    if (!replication_)
        return invalid;

    const networking::TransformHistoryEntry* entry =
        replication_->getSnapshotAtTick(tick);
    if (!entry) {
        LOG_WARN("DamageSystem: tick {} not in rewind history; raycasting "
                 "against live positions", tick);
        return invalid;
    }

    // Convert the networking history map into a flat EntityTransformSnapshot
    // span that LagCompensator can consume without touching the networking module.
    std::vector<engine::physics::EntityTransformSnapshot> snapshots;
    snapshots.reserve(entry->transforms.size());

    for (const auto& [netId, hist] : entry->transforms) {
        const core::ecs::Entity e = registry_.find(netId);
        if (e == core::ecs::kInvalidEntity) continue;
        snapshots.push_back({e, hist});
    }

    return lagComp_.rewindAndRaycast(snapshots, origin, dir, maxDist);
}


void DamageSystem::tick() {
    // Prune dedup entries older than 640 ticks (10 s at 64 Hz) before processing
    // the new batch so the map stays bounded regardless of match length.
    for (auto it = processed_.begin(); it != processed_.end(); ) {
        if (serverTick_ - it->second > 640u)
            it = processed_.erase(it);
        else
            ++it;
    }
    ++serverTick_;

    if (queue_.empty()) return;

    // Accumulate damage per target so concurrent hits commute (shotgun pellets,
    // two shooters): order-independent, clamped once at apply time.
    struct Accum {
        float    damage        = 0.0f;
        Entity   attacker      = kInvalidEntity;
        uint32_t attackerNetId = 0u;
        uint8_t  weaponType    = 0u;
    };
    std::unordered_map<uint32_t, Accum> perTarget;  // keyed by target netId

    for (const auto& pending : queue_) {
        const auto& req = pending.req;

        // Dedup by fireSerial — a reliable request may be delivered once, but a
        // resend before ACK can surface twice; ignore repeats.
        if (processed_.count(req.fireSerial))
            continue;
        processed_[req.fireSerial] = serverTick_;

        const Entity claimed = registry_.find(req.targetNetId);
        if (claimed == kInvalidEntity)
            continue;

        // Normalize the ray direction defensively; clients send it normalized
        // but quantization can leave it slightly off unit length.
        Vec3 dir = req.direction;
        const float len = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (len < 1e-4f)
            continue;
        dir /= len;

        constexpr float kMaxSinPitch = 0.99985f; // sin(89°)
        dir.y = std::clamp(dir.y, -kMaxSinPitch, kMaxSinPitch);
        const float lenAfter = std::sqrt(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
        if (lenAfter > 1e-4f) dir /= lenAfter;

        // Use LagCompensator to rewind entity positions to req.clientTick so
        // the raycast sees the world as the shooting client saw it.
        // If history is unavailable lagCompRaycast returns hasHit==false and
        // we fall back to a live-position raycast.
        engine::physics::RaycastHit hit = lagCompRaycast(req.clientTick,
                                                         req.origin, dir,
                                                         1000.0f);
        if (!hit.hasHit) {
            // Fallback: raycast against live positions.
            hit = physics_.raycast(req.origin, dir, 1000.0f);
        }

        if (!hit.hasHit)
            continue;
        if (hit.entity != claimed)
            continue;  // server disagrees with the client's claimed target

        Entity   attacker      = kInvalidEntity;
        uint32_t attackerNetId = 0u;
        if (const auto ait = attackers_.find(req.fireSerial); ait != attackers_.end()) {
            attacker      = ait->second.entity;
            attackerNetId = ait->second.netId;
        }

        const float dmg = weaponDamage(pending.weaponType);

        // Server-local consequence: hit confirmed.
        eventBus_.publish(HitscanHitEvent{
            claimed, attacker, req.fireSerial, pending.weaponType, dmg});

        Accum& acc = perTarget[req.targetNetId];
        acc.damage        += dmg;
        acc.attacker       = attacker;       // last attacker credited on kill
        acc.attackerNetId  = attackerNetId;
        acc.weaponType     = pending.weaponType;
    }

    queue_.clear();

    // Apply accumulated damage: shield absorbs first, then HP, clamped once.
    for (const auto& [targetNetId, acc] : perTarget) {
        const Entity target = registry_.find(targetNetId);
        if (target == kInvalidEntity)
            continue;

        auto* health = world_.tryGet<engine::core::Health>(target);
        if (!health)
            continue;
        if (health->currentHp <= 0.0f)
            continue;  // already dead this match; ignore

        float remaining = acc.damage;

        // shieldPercent in [0,1] is the fraction of damage the shield absorbs.
        if (health->shieldPercent > 0.0f && remaining > 0.0f) {
            const float absorbed = remaining * std::clamp(health->shieldPercent, 0.0f, 1.0f);
            remaining -= absorbed;
        }

        health->currentHp = std::max(0.0f, health->currentHp - remaining);

        if (health->currentHp <= 0.0f) {
            Vec3 deathPos = Vec3::zero();
            if (const auto* tr = world_.tryGet<engine::core::Transform>(target))
                deathPos = tr->position;

            eventBus_.publish(EntityDiedEvent{
                target, acc.attacker, acc.weaponType, deathPos});

            pendingDeathRpcs_.push_back(PlayerDiedPayload{
                targetNetId, acc.attackerNetId, acc.weaponType, deathPos});
        }
    }
}

} // namespace engine::app
