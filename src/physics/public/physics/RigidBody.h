#pragma once

#include <core/ecs/Entity.h>
#include <core/math/Vec.h>

namespace engine::physics {

enum class RigidBodyType : uint8_t {
    Static,
    Kinematic,
    Dynamic,
};

enum FreezeFlags : uint8_t {
    kFreezeNone    = 0,
    kFreezePosX    = 1 << 0,
    kFreezePosY    = 1 << 1,
    kFreezePosZ    = 1 << 2,
    kFreezeRotX    = 1 << 3,
    kFreezeRotY    = 1 << 4,
    kFreezeRotZ    = 1 << 5,
};

struct RigidBody {
    static constexpr engine::core::ecs::ComponentTypeId kComponentId = 6;

    RigidBodyType type            = RigidBodyType::Dynamic;
    float         mass            = 1.0f;
    float         linearDamping   = 0.05f;
    float         angularDamping  = 0.05f;
    float         friction        = 0.5f;
    float         restitution     = 0.1f;

    engine::core::math::Vec3 velocity        = engine::core::math::Vec3::zero();
    engine::core::math::Vec3 angularVelocity = engine::core::math::Vec3::zero();
    engine::core::math::Vec3 force           = engine::core::math::Vec3::zero();
    engine::core::math::Vec3 torque          = engine::core::math::Vec3::zero();

    uint8_t freezeFlags = kFreezeNone;
};

} // namespace engine::physics
