#pragma once

#include <cstdint>

namespace engine::rendering {

    // -------------------------------------------------------------------------
    // MaterialHandle
    // Lightweight index into the renderer's material pool.
    // -------------------------------------------------------------------------
    struct MaterialHandle {
        uint16_t index { 0xFFFFu };

        constexpr bool isValid() const noexcept { return index != 0xFFFFu; }

        constexpr bool operator==(const MaterialHandle& r) const noexcept { return index == r.index; }
        constexpr bool operator!=(const MaterialHandle& r) const noexcept { return index != r.index; }
    };

    inline constexpr MaterialHandle kInvalidMaterial{};

    // -------------------------------------------------------------------------
    // GpuMaterial  (64 bytes)
    // PBR material parameters stored in a GPU structured buffer.
    // Texture indices reference slots in the main CBV/SRV/UAV descriptor heap.
    // 0xFFFFFFFF (or 0 for the null SRV slot) means "no texture".
    // Metallic/roughness workflow follows the glTF convention:
    //   R = metallic, G = roughness stored in metallicRoughnessIndex texture.
    // -------------------------------------------------------------------------
    struct GpuMaterial {
        uint32_t albedoTextureIndex;          //  4 bytes
        uint32_t normalTextureIndex;          //  4 bytes
        uint32_t metallicRoughnessIndex;      //  4 bytes — R=metallic, G=roughness
        uint32_t emissiveTextureIndex;        //  4 bytes
        float    albedoFactor[4];             // 16 bytes — base color multiplier (RGBA)
        float    metallicFactor;              //  4 bytes
        float    roughnessFactor;             //  4 bytes
        float    emissiveFactor[3];           // 12 bytes
        float    pad_[3];                     // 12 bytes — explicit padding to 64 bytes
    };
    static_assert(sizeof(GpuMaterial) == 64,
        "GpuMaterial must be exactly 64 bytes");

} // namespace engine::rendering
