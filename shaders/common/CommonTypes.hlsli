#pragma once

// Coordinate convention: row-major, Y-up RH, +Z forward, reverse-Z depth.
// nearZ -> depth 1.0, farZ -> depth 0.0.
// Mat4 in HLSL is row_major float4x4.

struct PerFrameConstants {
    row_major float4x4 viewMatrix;
    row_major float4x4 projMatrix;
    row_major float4x4 viewProjMatrix;
    float3 cameraWorldPos;
    float  pad0;
    float  time;
    uint   lightCount;
    uint   hasIbl;    // non-zero when IBL SRVs are bound (gIrradianceMap, gPrefilteredEnv, gBrdfLut)
    uint   viewMode;  // 0=Lit, 1=Unlit (skips PBR/IBL, outputs raw albedo)

    // Shadow cascades (directional light only — at most 1 directional shadow caster).
    // shadowCascadeMat[i] transforms world-space positions into the light-space clip
    // space of cascade i.  Standard depth [0,1], NOT reverse-Z.
    row_major float4x4 shadowCascadeMat[4];
    // World-space (view-space distance) split boundaries for the 4 cascades.
    // A pixel belongs to cascade i when its linear depth < cascadeSplits[i].
    float4 cascadeSplits;
    uint   hasShadows;  // 0 = no shadow maps active; non-zero = cascade maps are valid
    float  pad2[3];     // pad to 16-byte alignment
};

struct PerObjectConstants {
    row_major float4x4 worldMatrix;
    uint   materialIndex;   // index into materials[] structured buffer
    float3 pad;
};

// Matches engine::rendering::GpuMaterial (64 bytes).
// glTF metallic/roughness workflow: metallicRoughnessIndex texture R=metallic, G=roughness.
// Texture indices reference slots in the bindless SRV heap; 0xFFFFFFFF = no texture.
struct GpuMaterial {
    uint   albedoTextureIndex;          // SRV index into bindless heap
    uint   normalTextureIndex;
    uint   metallicRoughnessIndex;      // R=metallic, G=roughness
    uint   emissiveTextureIndex;
    float4 albedoFactor;                // base color multiplier (RGBA)
    float  metallicFactor;
    float  roughnessFactor;
    float3 emissiveFactor;
    float  pad_;
};  // 64 bytes

// Matches engine::rendering::GpuLight (64 bytes).
// position.w = type (0=directional, 1=point, 2=spot)
// direction.w = range
// color.w = innerCosAngle
// spotAngles: x=cos(outerCone), y=1/(cos(inner)-cos(outer)), zw=shadow map index (-1=no shadow)
struct GpuLight {
    float4 position;      // xyz=world pos or unused for directional, w=type
    float4 direction;     // xyz=normalized direction (world space), w=range
    float4 color;         // rgb=color*intensity (linear), w=innerCosAngle
    float4 spotAngles;    // x=cos(outerCone), y=1/(cosInner-cosOuter), z=shadowIdx, w=unused
};  // 64 bytes
