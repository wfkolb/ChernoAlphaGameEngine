# Rendering: PBR Lighting, Shadows, Camera, and Debug Draw

Status: Updated (Phase 10)
Owner: Rendering Lead
Task: #9
References: architecture.md §2, rendering-mesh-material-shader.md, scope-rendering.md

---

## 1. PBR Lighting Model

### 1.1 BRDF: Cook-Torrance GGX

The engine uses the Cook-Torrance specular BRDF with GGX normal distribution, Smith-Schlick visibility, and Schlick Fresnel. Combined with Lambertian diffuse.

```hlsl
// Normal Distribution Function — GGX/Trowbridge-Reitz
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Geometric Shadowing — Smith-Schlick-GGX
float G_Smith(float NdotV, float NdotL, float roughness) {
    float r  = roughness + 1.0f;
    float k  = (r * r) / 8.0f;
    float gV = NdotV / (NdotV * (1.0f - k) + k);
    float gL = NdotL / (NdotL * (1.0f - k) + k);
    return gV * gL;
}

// Fresnel — Schlick approximation
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}
```

Final radiance per light:
```hlsl
float3 Lo = (albedo / PI + specular) * radiance * NdotL;
```

Metallic/roughness workflow (glTF convention): `F0 = lerp(0.04, albedo, metallic)`.

### 1.2 Image-Based Lighting (IBL)

Two precomputed IBL resources per skybox:
- **Prefiltered environment map**: cubemap, `DXGI_FORMAT_R16G16B16A16_FLOAT`, 7 mip levels (roughness 0..1 mapped across mips). Produced by the asset importer offline.
- **BRDF LUT**: 2D texture, `DXGI_FORMAT_R16G16_UNORM`, 256×256. Generated offline (stored in `.easset`). X axis = NdotV (0..1), Y axis = roughness (0..1), RG = scale/bias for split-sum approximation.

IBL contribution:
```hlsl
float3 F  = F_Schlick_Roughness(NdotV, F0, roughness);
float3 kS = F;
float3 kD = (1.0f - kS) * (1.0f - metallic);

float3 irradiance    = irradianceMap.Sample(LinearClamp, N).rgb;
float3 diffuseIBL    = irradiance * albedo * kD;

float3 prefilteredColor = prefilteredEnvMap.SampleLevel(LinearClamp, R, roughness * MAX_REFLECTION_LOD).rgb;
float2 brdfLUT       = brdfLutTex.Sample(LinearClamp, float2(NdotV, roughness)).rg;
float3 specularIBL   = prefilteredColor * (F * brdfLUT.x + brdfLUT.y);

float3 ambient = diffuseIBL + specularIBL;
```

`MAX_REFLECTION_LOD = 6.0` (7 mip levels, 0-indexed).

---

## 2. Light Components

### 2.1 Light ECS component

```cpp
struct Light {
    enum class Type : uint8_t { Directional, Point, Spot };

    Type    type            { Type::Directional };
    float   color[3]        { 1.0f, 1.0f, 1.0f };   // linear RGB
    float   intensity       { 1.0f };                 // lux (directional), candela (point/spot)
    float   range           { 10.0f };                // point/spot only, in meters
    float   innerConeAngle  { 0.0f };                 // spot only, radians
    float   outerConeAngle  { 0.785f };               // spot only, radians (~45°)
    bool    castShadow      { false };
};
```

Direction is derived from the entity's `Transform::rotation`. Position from `Transform::position`.

### 2.2 GPU light buffer

```cpp
struct GpuLight {
    float position[4];          // w = type (0=dir, 1=point, 2=spot)
    float direction[4];         // xyz = normalized direction, w = range
    float color[4];             // rgb = color * intensity, a = innerCosAngle
    float spotAngles[4];        // x = cos(outerCone), y = 1/(cos(inner)-cos(outer)), zw = shadow map index (-1 = no shadow)
};
static_assert(sizeof(GpuLight) == 64);
```

Max 64 lights per frame in v1 (`kMaxLights = 64`). Stored in a per-frame upload-heap structured buffer.

---

## 3. Shadow Maps

### 3.1 Directional light: Cascaded Shadow Maps (CSM)

- 4 cascades (`kShadowCascadeCount = 4`).
- Shadow map per cascade: `DXGI_FORMAT_D32_FLOAT`, 2048×2048 (`kShadowMapResolution = 2048`).
- Stored as a 2D texture array: `kShadowCascadeCount` slices.

Cascade split scheme: practical split (blend of logarithmic and uniform):
```
splitLambda = 0.95 (configurable via [render].shadowSplitLambda)
near_i = nearZ * (farZ / nearZ)^(i / N) * splitLambda
        + nearZ + (farZ - nearZ) * (i / N) * (1 - splitLambda)
```

Each cascade's view-projection matrix is stored in the per-frame CB, fitting a tight orthographic frustum around the camera frustum slice, snapped to texel boundaries (to avoid shimmer). Stable shadow mapping: use the light's view space, not world space, for snapping.

Reverse-Z is NOT used for shadow maps (shadow comparison direction is reversed anyway). Shadow maps use standard depth [0,1], near=0.

### 3.2 Point and spot lights: single shadow map

- One shadow map per shadow-casting point/spot (`DXGI_FORMAT_D32_FLOAT`, 512×512 each in v1).
- Up to 8 shadow-casting non-directional lights in v1 (`kMaxShadowCastingSpots = 8`).
- Point lights use a cube map (6 faces); spot lights use a single 2D map.
- Shadow cube maps stored as `D3D12_RESOURCE_DIMENSION_TEXTURE2D` with 6 array slices.

### 3.3 Shadow sampling

In the lighting pass, shadows are sampled with PCF (4-tap 2×2 kernel):
```hlsl
float shadow = shadowMap.SampleCmpLevelZero(ShadowPCF, shadowUV, compareDepth);
```

The `ShadowPCF` sampler (slot 5 in the sampler heap) uses `D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT` with `D3D12_COMPARISON_FUNC_LESS_EQUAL`.

---

## 4. Camera System

### 4.1 Camera ECS component

```cpp
struct Camera {
    float fovYDegrees { 60.0f };
    float nearZ       {  0.1f };
    float farZ        { 1000.0f };
    bool  isMain      { true };   // the renderer renders from the first entity where isMain=true
};
```

### 4.2 Projection matrix

Perspective (reverse-Z, right-handed, Y-up):

```
near maps to depth 1.0, far maps to 0.0
```

Using `core::math::perspectiveRhYupReverseZ(fovY, aspect, nearZ, farZ)` from Mat.h.

The reverse-Z convention improves depth precision for distant geometry. All depth comparisons in shaders are `LESS_EQUAL` with `clearDepth = 0.0f` (far).

The depth buffer format is `DXGI_FORMAT_D32_FLOAT` (no stencil in v1).

### 4.3 Frustum extraction

Given the combined view-projection matrix `VP`, frustum planes are extracted using Gribb-Hartmann method (normalize by dividing by the length of the (A,B,C) normal):

```cpp
// Extracted in world space from the transposed VP matrix
// Using core::math::Frustum (6 planes, Vec4 each: xyz=normal, w=distance)
Frustum extractFrustum(const Mat4& viewProjection) noexcept;
```

This function lives in `core::math` (the rendering module calls it; the ECS design doc §3.1 lists Frustum as a core type).

### 4.4 FPS camera controller component

Implemented in Phase 10 (R6). Registered as ECS component ID 17 (`rendering::FpsCameraController`).

```cpp
struct FpsCameraController {
    float moveSpeed       { 5.0f };   // m/s
    float lookSensitivity { 0.1f };   // degrees per raw mouse unit
    float yaw             { 0.0f };   // degrees
    float pitch           { 0.0f };   // degrees, clamped ±89°
    bool  active          { true };
};
```

`FpsCameraSystem` is registered in the `Update` phase. It reads `InputSystem::currentFrame()` (which now includes gamepad state — see N1), integrates look and move deltas, and writes `Transform::position` and `Transform::rotation` on the same entity. Mouse look uses raw input delta, not cursor position.

Movement: WASD + Q/E (up/down). Sprint: Shift. Left gamepad stick controls look (yaw/pitch), right stick controls lateral/forward move. No physics — direct position integration.

The `Camera` component (ID 16) was also formally registered in Phase 10 (R6); it was previously used without registration in Engine.cpp.

---

## 5. Debug Draw

### 5.1 Public API (callable from any module)

```cpp
// src/rendering/public/rendering/DebugDraw.h
namespace engine::rendering::DebugDraw {
    void line  (Vec3 start, Vec3 end,   Vec4 color = {1,1,1,1});
    void sphere(Vec3 center, float radius, Vec4 color = {1,1,0,1});
    void box   (Vec3 center, Vec3 halfExtents, Quat rotation, Vec4 color = {0,1,1,1});
    void aabb  (const AABB& aabb, Vec4 color = {0,1,0,1});
    void text  (Vec3 worldPos, std::string_view str, Vec4 color = {1,1,1,1});
    void flush ();   // called by the renderer at end of PostRender phase
}
```

`Vec3`, `Vec4`, `Quat`, `AABB` are from `core::math`. No DX12 types in this header.

### 5.2 Implementation

`DebugDraw.cpp` accumulates `DebugVertex` structs in a per-frame CPU buffer. `shaders/debug/DebugLineVS.hlsl` and `shaders/debug/DebugLinePS.hlsl` were created in Phase 10 (R5) and compile successfully.

Line vertex (GPU layout):
```cpp
struct DebugVertex { float xyz[3]; uint32_t packedColor; };  // 16 bytes
```

Color packed as `RGBA8_UNORM`. The pass uses `D3D12_PRIMITIVE_TOPOLOGY_LINELIST`.

**Current status (Phase 10 R5):** CPU accumulation is wired — `line()`, `sphere()`, `box()`, `aabb()` all append `DebugVertex` pairs. `flush()` is a stub: it logs the vertex count when no command list is set. Full GPU upload and FrameGraph pass registration is a `TODO Phase 10 R5` pending FrameGraph integration. `text()` is also a stub (logs, no billboard geometry).

Max debug primitives per frame: `kMaxDebugPrimitives = 65536` (hard cap; excess is silently dropped with a `LOG_WARN`, once per frame maximum).

### 5.3 Text rendering

Text uses a simple bitmap font (8×8 pixel glyphs) stored as a texture atlas in the `shaders/debug/font.png` (cooked to `.easset`). World-position text is rendered as a screen-space billboard quad in the debug pass. Up to 256 text labels per frame.

---

## 6. Rendering Module Bootstrap

Registered components (called from `app::BootstrapOrder`):
```cpp
world.registerComponent<rendering::Camera>("Camera");
world.registerComponent<rendering::Light>("Light");
world.registerComponent<rendering::Renderable>("Renderable");
world.registerComponent<rendering::FpsCameraController>("FpsCameraController");
```

Registered systems:
```cpp
world.addSystem("RenderCull",  SystemPhase::Render, renderCullSystem);
world.addSystem("RenderSubmit",SystemPhase::Render, renderSubmitSystem);
world.addSystem("FpsCamera",   SystemPhase::Update, fpsCameraSystem);
world.addSystem("DebugFlush",  SystemPhase::PostRender, debugFlushSystem);
```

---

## 7. Performance Targets (reminders from scope-rendering.md)

| Metric | Target |
|---|---|
| Empty frame (clear + present), 1080p | ≤ 0.5 ms GPU |
| 10k static mesh draws | ≤ 4 ms CPU recording |
| Shader hot-reload | ≤ 250 ms |
| Frame graph compile | ≤ 0.05 ms CPU |

Shadow map resolution and cascade count are runtime-configurable via `[render]` config to aid hitting these targets on lower-end hardware.
