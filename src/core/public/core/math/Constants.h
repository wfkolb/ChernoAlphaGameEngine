#pragma once

#include <limits>

namespace engine::core::math {

    inline constexpr float kPi              = 3.14159265358979323846f;
    inline constexpr float kTwoPi           = 6.28318530717958647692f;
    inline constexpr float kHalfPi          = 1.57079632679489661923f;
    inline constexpr float kInvPi           = 0.31830988618379067154f;
    inline constexpr float kDegToRad        = kPi / 180.0f;
    inline constexpr float kRadToDeg        = 180.0f / kPi;
    inline constexpr float kEpsilon         = 1.0e-6f;
    inline constexpr float kEpsilonNormalSq = 1.0e-12f;
    inline constexpr float kInfinity        = std::numeric_limits<float>::infinity();

    constexpr float toRadians(float degrees) noexcept { return degrees * kDegToRad; }
    constexpr float toDegrees(float radians) noexcept { return radians * kRadToDeg; }

    constexpr float clamp(float v, float lo, float hi) noexcept {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    constexpr float saturate(float v) noexcept { return clamp(v, 0.0f, 1.0f); }

    constexpr float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

    inline bool nearlyEqual(float a, float b, float eps = kEpsilon) noexcept {
        const float d = a - b;
        return (d < 0.0f ? -d : d) <= eps;
    }

}
