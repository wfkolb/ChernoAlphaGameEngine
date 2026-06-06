#pragma once
#include <core/ecs/World.h>
#include <core/ecs/Entity.h>
#include <core/components/Health.h>
#include <core/components/Transform.h>
#include <core/math/Vec.h>
#include <networking/HitscanValidationRequest.h>
#include <networking/NetworkRegistry.h>
#include <networking/ReplicationSystem.h>
#include <networking/RPC.h>
#include <physics/LagCompensator.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::core { class EventBus; }

namespace engine::app {

// Posted on the server EventBus when a hitscan request is validated as a hit.
struct HitscanHitEvent {
    engine::core::ecs::Entity target   = engine::core::ecs::kInvalidEntity;
    engine::core::ecs::Entity attacker = engine::core::ecs::kInvalidEntity;
    uint32_t                  fireSerial  = 0u;
    uint8_t                   weaponType  = 0u;
    float                     damage      = 0.0f;
};

// Posted on the server EventBus after Health reaches zero this tick.
struct EntityDiedEvent {
    engine::core::ecs::Entity dead          = engine::core::ecs::kInvalidEntity;
    engine::core::ecs::Entity killer        = engine::core::ecs::kInvalidEntity;
    uint8_t                   weaponType    = 0u;
    engine::core::math::Vec3  deathPosition = engine::core::math::Vec3::zero();
};

// Server-only damage authority. Consumes reliable HitscanValidationRequests,
// validates them against the physics world (with lag-compensation rewind when
// a ReplicationSystem is provided), then applies damage commutatively: all hits
// on a target this tick are summed and clamped once, shield absorbs before HP.
//
// Pass a non-null replicationSystem to enable lag-comp rewind: entity physics
// bodies are temporarily moved to their positions at req.clientTick via
// LagCompensator::rewindAndRaycast(), then restored automatically.
// If replicationSystem is null or the tick is out of range the raycast falls
// back to current positions.
//
// On a kill the system publishes EntityDiedEvent locally and emits a reliable
// PlayerDied RPC to all clients.
class DamageSystem {
public:
    // PlayerDied: discrete, non-supersedable. Reliable to every client so kill
    // feed / death camera / respawn UI never miss it.
    ENGINE_RPC(PlayerDied, AllClients, Reliable);

    // replicationSystem may be nullptr (disables lag-comp; used in tests and
    // for client-side instances that do not own a ReplicationSystem).
    DamageSystem(engine::core::ecs::World&              world,
                 engine::physics::PhysicsWorld&         physics,
                 engine::networking::NetworkRegistry&   registry,
                 engine::core::EventBus&                eventBus,
                 engine::networking::ReplicationSystem* replicationSystem = nullptr);

    void setWeaponDamage(uint8_t weaponType, float damage);
    float weaponDamage(uint8_t weaponType) const;

    // The attacker associated with a fired shot, used to attribute kills.
    void setAttackerForRequest(uint32_t fireSerial,
                               engine::core::ecs::Entity attacker,
                               uint32_t attackerNetId = 0u);

    // Networking layer feeds validated-as-received requests here each tick.
    void submitRequest(const engine::networking::HitscanValidationRequest& req,
                       uint8_t weaponType = 0u);

    // Process all queued requests: dedup, rewind, raycast, accumulate + apply
    // damage, emit death events/RPCs. Call once per server tick after step().
    void tick();

    // Outbound reliable RPC payloads produced this tick (drained by the session).
    struct PlayerDiedPayload {
        uint32_t                 deadNetId    = 0u;
        uint32_t                 killerNetId  = 0u;
        uint8_t                  weaponType   = 0u;
        engine::core::math::Vec3 deathPosition = engine::core::math::Vec3::zero();
    };
    const std::vector<PlayerDiedPayload>& pendingDeathRpcs() const noexcept {
        return pendingDeathRpcs_;
    }
    void clearPendingDeathRpcs() noexcept { pendingDeathRpcs_.clear(); }

    size_t processedCount() const noexcept { return processed_.size(); }

private:
    struct PendingRequest {
        engine::networking::HitscanValidationRequest req;
        uint8_t                                      weaponType;
    };

    struct AttackerInfo {
        engine::core::ecs::Entity entity = engine::core::ecs::kInvalidEntity;
        uint32_t                  netId  = 0u;
    };

    engine::core::ecs::World&                    world_;
    engine::physics::PhysicsWorld&               physics_;
    engine::networking::NetworkRegistry&         registry_;
    engine::core::EventBus&                      eventBus_;
    engine::networking::ReplicationSystem* const replication_;
    engine::physics::LagCompensator              lagComp_;

    std::vector<PendingRequest>                  queue_;
    std::unordered_map<uint32_t, uint32_t>       processed_; // serial → serverTick at receipt
    uint32_t                                     serverTick_ = 0u;
    std::unordered_map<uint32_t, AttackerInfo>   attackers_;
    std::unordered_map<uint8_t, float>       weaponDamage_;
    std::vector<PlayerDiedPayload>           pendingDeathRpcs_;

    // Build an EntityTransformSnapshot list from replication history at `tick`
    // and invoke LagCompensator::rewindAndRaycast().
    // Returns an invalid RaycastHit (hasHit == false) when history is
    // unavailable; the caller should fall back to a live-position raycast.
    engine::physics::RaycastHit lagCompRaycast(
        uint32_t                        tick,
        const engine::core::math::Vec3& origin,
        const engine::core::math::Vec3& dir,
        float                           maxDist);
};

} // namespace engine::app
