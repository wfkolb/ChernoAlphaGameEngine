#pragma once
#include <core/ecs/World.h>
#include <core/ecs/Entity.h>
#include <core/components/Health.h>
#include <core/math/Vec.h>
#include <networking/HitscanValidationRequest.h>
#include <networking/NetworkRegistry.h>
#include <networking/RPC.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::core { class EventBus; }
namespace engine::physics { class PhysicsWorld; }

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
// validates them against the live physics world (no lag-comp rewind in Phase 7
// — only a placeholder history ring buffer), then applies damage commutatively:
// all hits on a target this tick are summed and clamped once, with the shield
// absorbing before HP. This mirrors the spec's CommandBuffer::Delta<Health>
// intent without mutating shared core code.
//
// On a kill the system publishes EntityDiedEvent locally and emits a reliable
// PlayerDied RPC to all clients.
class DamageSystem {
public:
    // PlayerDied: discrete, non-supersedable. Reliable to every client so kill
    // feed / death camera / respawn UI never miss it.
    ENGINE_RPC(PlayerDied, AllClients, Reliable);

    DamageSystem(engine::core::ecs::World&        world,
                 engine::physics::PhysicsWorld&   physics,
                 engine::networking::NetworkRegistry& registry,
                 engine::core::EventBus&          eventBus);

    void setWeaponDamage(uint8_t weaponType, float damage);
    float weaponDamage(uint8_t weaponType) const;

    // The attacker associated with a fired shot, used to attribute kills.
    // attackerNetId is the shooter's replicated id (0 if not networked).
    // Defaults to kInvalidEntity / 0 if unset.
    void setAttackerForRequest(uint32_t fireSerial,
                               engine::core::ecs::Entity attacker,
                               uint32_t attackerNetId = 0u);

    // Networking layer feeds validated-as-received requests here each tick.
    void submitRequest(const engine::networking::HitscanValidationRequest& req,
                       uint8_t weaponType = 0u);

    // Record a target's position for the given tick into the placeholder
    // lag-comp ring buffer. Phase 7 never rewinds to these samples — the buffer
    // exists so the wire/API shape is stable when rewind lands in a later phase.
    void recordHistory(uint32_t tick, engine::core::math::Vec3 position);

    // Process all queued requests: dedup, raycast, accumulate + apply damage,
    // emit death events/RPCs. Call once per server tick after PhysicsWorld::step.
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

    // Placeholder lag-comp history: positions are recorded but never rewound to
    // in Phase 7 (validation uses current world state).
    struct HistorySample {
        uint32_t                 tick = 0u;
        engine::core::math::Vec3 position = engine::core::math::Vec3::zero();
    };
    static constexpr size_t kHistoryRing = 6;  // ~kRewindWindowTicks placeholder

    struct AttackerInfo {
        engine::core::ecs::Entity entity = engine::core::ecs::kInvalidEntity;
        uint32_t                  netId  = 0u;
    };

    engine::core::ecs::World&            world_;
    engine::physics::PhysicsWorld&       physics_;
    engine::networking::NetworkRegistry& registry_;
    engine::core::EventBus&              eventBus_;

    std::vector<PendingRequest>        queue_;
    std::unordered_set<uint32_t>       processed_;          // dedup by fireSerial
    std::unordered_map<uint32_t, AttackerInfo> attackers_; // fireSerial → attacker
    std::unordered_map<uint8_t, float> weaponDamage_;

    HistorySample historyRing_[kHistoryRing] = {};
    size_t        historyHead_ = 0;

    std::vector<PlayerDiedPayload> pendingDeathRpcs_;
};

} // namespace engine::app
