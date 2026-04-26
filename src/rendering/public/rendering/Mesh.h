#pragma once

#include <core/math/Vec.h>
#include <cstdint>

namespace engine::rendering {

    // -------------------------------------------------------------------------
    // Vertex layout identifiers
    // -------------------------------------------------------------------------
    inline constexpr uint8_t kVertexLayoutPosition = 0; // positions only (shadow / depth pre-pass)
    inline constexpr uint8_t kVertexLayoutStatic   = 1; // full static mesh
    inline constexpr uint8_t kVertexLayoutSkinned  = 2; // skinned mesh (forward-compatible; v1 unused)

    // -------------------------------------------------------------------------
    // VertexPosition  (12 bytes)
    // Used for shadow passes and depth pre-pass.
    // -------------------------------------------------------------------------
    struct VertexPosition {
        float x, y, z;
    };
    static_assert(sizeof(VertexPosition) == 12,
        "VertexPosition must be exactly 12 bytes");

    // -------------------------------------------------------------------------
    // VertexStatic  (28 bytes, 4-byte aligned)
    // packedNormal  : R10G10B10A2_UNORM; w component unused.
    // packedTangent : R10G10B10A2_UNORM; w bit31 = sign(bitangent).
    //                 Bitangent reconstructed in shader: cross(N, T.xyz) * T.w
    // -------------------------------------------------------------------------
    struct VertexStatic {
        float    position[3];   // 12 bytes
        uint32_t packedNormal;  //  4 bytes — 10/10/10/2 UNORM
        uint32_t packedTangent; //  4 bytes — 10/10/10/2 UNORM, bit31 = bitangent sign
        float    uv[2];         //  8 bytes
    };
    static_assert(sizeof(VertexStatic) == 28,
        "VertexStatic must be exactly 28 bytes");

    // -------------------------------------------------------------------------
    // VertexSkinned  (36 bytes)
    // boneWeights: UNORM8; last weight = 1 - sum(others) (not stored explicitly).
    // -------------------------------------------------------------------------
    struct VertexSkinned {
        VertexStatic base;          // 28 bytes
        uint8_t      boneIndices[4]; //  4 bytes
        uint8_t      boneWeights[4]; //  4 bytes — UNORM8
    };
    static_assert(sizeof(VertexSkinned) == 36,
        "VertexSkinned must be exactly 36 bytes");

    // -------------------------------------------------------------------------
    // MeshHandle
    // Identifies a (vertex buffer, index buffer, vertex layout, index count, AABB)
    // tuple stored in the renderer's mesh registry.
    // -------------------------------------------------------------------------
    struct MeshHandle {
        uint32_t id { 0xFFFF'FFFFu };

        constexpr bool isValid() const noexcept { return id != 0xFFFF'FFFFu; }

        constexpr bool operator==(const MeshHandle& r) const noexcept { return id == r.id; }
        constexpr bool operator!=(const MeshHandle& r) const noexcept { return id != r.id; }
    };

    inline constexpr MeshHandle kInvalidMesh{};

} // namespace engine::rendering
