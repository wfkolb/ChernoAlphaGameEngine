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
    float  pad1[3];
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
