#pragma once

#include "core/math/Constants.h"

#include <cmath>

namespace engine::core::math {

    struct Vec2 {
        float x{0.0f};
        float y{0.0f};

        constexpr Vec2() = default;
        constexpr Vec2(float xx, float yy) noexcept : x(xx), y(yy) {}
        explicit constexpr Vec2(float v) noexcept : x(v), y(v) {}

        constexpr Vec2 operator+(const Vec2& r) const noexcept { return {x + r.x, y + r.y}; }
        constexpr Vec2 operator-(const Vec2& r) const noexcept { return {x - r.x, y - r.y}; }
        constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
        constexpr Vec2 operator*(const Vec2& r) const noexcept { return {x * r.x, y * r.y}; }
        constexpr Vec2 operator/(float s) const noexcept { return {x / s, y / s}; }
        constexpr Vec2 operator-() const noexcept { return {-x, -y}; }

        Vec2& operator+=(const Vec2& r) noexcept { x += r.x; y += r.y; return *this; }
        Vec2& operator-=(const Vec2& r) noexcept { x -= r.x; y -= r.y; return *this; }
        Vec2& operator*=(float s) noexcept { x *= s; y *= s; return *this; }
        Vec2& operator/=(float s) noexcept { x /= s; y /= s; return *this; }

        constexpr bool operator==(const Vec2& r) const noexcept { return x == r.x && y == r.y; }
        constexpr bool operator!=(const Vec2& r) const noexcept { return !(*this == r); }
    };

    constexpr Vec2 operator*(float s, const Vec2& v) noexcept { return v * s; }

    struct alignas(16) Vec3 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float pad_{0.0f};

        constexpr Vec3() = default;
        constexpr Vec3(float xx, float yy, float zz) noexcept : x(xx), y(yy), z(zz), pad_(0.0f) {}
        explicit constexpr Vec3(float v) noexcept : x(v), y(v), z(v), pad_(0.0f) {}

        constexpr Vec3 operator+(const Vec3& r) const noexcept { return {x + r.x, y + r.y, z + r.z}; }
        constexpr Vec3 operator-(const Vec3& r) const noexcept { return {x - r.x, y - r.y, z - r.z}; }
        constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
        constexpr Vec3 operator*(const Vec3& r) const noexcept { return {x * r.x, y * r.y, z * r.z}; }
        constexpr Vec3 operator/(float s) const noexcept { return {x / s, y / s, z / s}; }
        constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }

        Vec3& operator+=(const Vec3& r) noexcept { x += r.x; y += r.y; z += r.z; return *this; }
        Vec3& operator-=(const Vec3& r) noexcept { x -= r.x; y -= r.y; z -= r.z; return *this; }
        Vec3& operator*=(float s) noexcept { x *= s; y *= s; z *= s; return *this; }
        Vec3& operator/=(float s) noexcept { x /= s; y /= s; z /= s; return *this; }

        constexpr bool operator==(const Vec3& r) const noexcept { return x == r.x && y == r.y && z == r.z; }
        constexpr bool operator!=(const Vec3& r) const noexcept { return !(*this == r); }

        static constexpr Vec3 zero()    noexcept { return {0.0f, 0.0f, 0.0f}; }
        static constexpr Vec3 one()     noexcept { return {1.0f, 1.0f, 1.0f}; }
        static constexpr Vec3 unitX()   noexcept { return {1.0f, 0.0f, 0.0f}; }
        static constexpr Vec3 unitY()   noexcept { return {0.0f, 1.0f, 0.0f}; }
        static constexpr Vec3 unitZ()   noexcept { return {0.0f, 0.0f, 1.0f}; }
        static constexpr Vec3 forward() noexcept { return {0.0f, 0.0f, 1.0f}; }   // +Z forward (RH)
        static constexpr Vec3 up()      noexcept { return {0.0f, 1.0f, 0.0f}; }
        static constexpr Vec3 right()   noexcept { return {1.0f, 0.0f, 0.0f}; }
    };

    constexpr Vec3 operator*(float s, const Vec3& v) noexcept { return v * s; }

    struct alignas(16) Vec4 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
        float w{0.0f};

        constexpr Vec4() = default;
        constexpr Vec4(float xx, float yy, float zz, float ww) noexcept : x(xx), y(yy), z(zz), w(ww) {}
        constexpr Vec4(const Vec3& v, float ww) noexcept : x(v.x), y(v.y), z(v.z), w(ww) {}
        explicit constexpr Vec4(float v) noexcept : x(v), y(v), z(v), w(v) {}

        constexpr Vec4 operator+(const Vec4& r) const noexcept { return {x + r.x, y + r.y, z + r.z, w + r.w}; }
        constexpr Vec4 operator-(const Vec4& r) const noexcept { return {x - r.x, y - r.y, z - r.z, w - r.w}; }
        constexpr Vec4 operator*(float s) const noexcept { return {x * s, y * s, z * s, w * s}; }
        constexpr Vec4 operator/(float s) const noexcept { return {x / s, y / s, z / s, w / s}; }
        constexpr Vec4 operator-() const noexcept { return {-x, -y, -z, -w}; }

        Vec4& operator+=(const Vec4& r) noexcept { x += r.x; y += r.y; z += r.z; w += r.w; return *this; }
        Vec4& operator-=(const Vec4& r) noexcept { x -= r.x; y -= r.y; z -= r.z; w -= r.w; return *this; }
        Vec4& operator*=(float s) noexcept { x *= s; y *= s; z *= s; w *= s; return *this; }

        constexpr bool operator==(const Vec4& r) const noexcept { return x == r.x && y == r.y && z == r.z && w == r.w; }
        constexpr bool operator!=(const Vec4& r) const noexcept { return !(*this == r); }

        constexpr Vec3 xyz() const noexcept { return {x, y, z}; }
    };

    constexpr Vec4 operator*(float s, const Vec4& v) noexcept { return v * s; }

    constexpr float dot(const Vec2& a, const Vec2& b) noexcept { return a.x * b.x + a.y * b.y; }
    constexpr float dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
    constexpr float dot(const Vec4& a, const Vec4& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

    constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    inline float lengthSquared(const Vec2& v) noexcept { return dot(v, v); }
    inline float lengthSquared(const Vec3& v) noexcept { return dot(v, v); }
    inline float lengthSquared(const Vec4& v) noexcept { return dot(v, v); }

    inline float length(const Vec2& v) noexcept { return std::sqrt(lengthSquared(v)); }
    inline float length(const Vec3& v) noexcept { return std::sqrt(lengthSquared(v)); }
    inline float length(const Vec4& v) noexcept { return std::sqrt(lengthSquared(v)); }

    inline Vec2 normalize(const Vec2& v) noexcept {
        const float ls = lengthSquared(v);
        if (ls < kEpsilonNormalSq) return {0.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(ls);
        return v * inv;
    }
    inline Vec3 normalize(const Vec3& v) noexcept {
        const float ls = lengthSquared(v);
        if (ls < kEpsilonNormalSq) return {0.0f, 0.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(ls);
        return v * inv;
    }
    inline Vec4 normalize(const Vec4& v) noexcept {
        const float ls = lengthSquared(v);
        if (ls < kEpsilonNormalSq) return {0.0f, 0.0f, 0.0f, 0.0f};
        const float inv = 1.0f / std::sqrt(ls);
        return v * inv;
    }

    constexpr Vec2 lerp(const Vec2& a, const Vec2& b, float t) noexcept { return a + (b - a) * t; }
    constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) noexcept { return a + (b - a) * t; }
    constexpr Vec4 lerp(const Vec4& a, const Vec4& b, float t) noexcept { return a + (b - a) * t; }

    inline float distance(const Vec3& a, const Vec3& b) noexcept { return length(b - a); }
    inline float distanceSquared(const Vec3& a, const Vec3& b) noexcept { return lengthSquared(b - a); }

    constexpr Vec3 minPerComponent(const Vec3& a, const Vec3& b) noexcept {
        return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
    }
    constexpr Vec3 maxPerComponent(const Vec3& a, const Vec3& b) noexcept {
        return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
    }

    constexpr Vec3 reflect(const Vec3& v, const Vec3& n) noexcept {
        return v - n * (2.0f * dot(v, n));
    }

}
