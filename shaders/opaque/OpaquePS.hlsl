#include "../common/CommonTypes.hlsli"

ConstantBuffer<PerFrameConstants> gPerFrame : register(b0);

// Bindless structured buffer of all materials at t0, space0.
StructuredBuffer<GpuMaterial> gMaterials : register(t0, space0);
// Per-frame light array (up to 64 lights) at t1, space0.
StructuredBuffer<GpuLight> gLights : register(t1, space0);
// IBL resources (bound when gPerFrame.hasIbl != 0)
TextureCube  gIrradianceMap  : register(t2, space0);
TextureCube  gPrefilteredEnv : register(t3, space0);
Texture2D    gBrdfLut        : register(t4, space0);
// Shadow maps (bound when gPerFrame.hasShadows != 0; stub 1x1 white when inactive)
Texture2DArray gShadowCascades : register(t5, space0);  // 4-slice D32 array for CSM
Texture2DArray gShadowSpots    : register(t6, space0);  // up to 8 slices for spot/point maps
// Bindless texture array at t1+, space1.
Texture2D gTextures[] : register(t1, space1);

SamplerState           gSampler      : register(s0);  // LinearWrap
SamplerState           gLinearClamp  : register(s1);  // LinearClamp (for IBL lookups)
SamplerComparisonState gShadowPCF    : register(s2);  // ComparisonLessEqual (shadow PCF)

static const float PI = 3.14159265f;
static const uint  kNoTexture = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// Shadow helpers
// ---------------------------------------------------------------------------

// 2x2 PCF kernel over the CSM cascade array.
// shadowCoord: homogeneous light-space position (output of shadowCascadeMat[i] * worldPos).
// cascadeIndex: which cascade slice to sample (0..3).
// Returns shadow factor in [0, 1]; 1.0 = fully lit, 0.0 = fully shadowed.
float SampleShadowCascade(float4 shadowCoord, uint cascadeIndex) {
    float3 projCoord = shadowCoord.xyz / shadowCoord.w;
    // Shadow maps use standard depth [0,1] (not reverse-Z).
    // projCoord.xy is in NDC [-1,1]; remap to UV [0,1].
    float2 uv    = projCoord.xy * 0.5f + 0.5f;
    float  depth = projCoord.z;

    // 2x2 PCF — 4 taps at ±half-texel offsets.
    static const float kTexelSize = 1.0f / 2048.0f;
    float shadow = 0.0f;
    shadow += gShadowCascades.SampleCmpLevelZero(gShadowPCF,
        float3(uv + float2(-kTexelSize, -kTexelSize), (float)cascadeIndex), depth);
    shadow += gShadowCascades.SampleCmpLevelZero(gShadowPCF,
        float3(uv + float2( kTexelSize, -kTexelSize), (float)cascadeIndex), depth);
    shadow += gShadowCascades.SampleCmpLevelZero(gShadowPCF,
        float3(uv + float2(-kTexelSize,  kTexelSize), (float)cascadeIndex), depth);
    shadow += gShadowCascades.SampleCmpLevelZero(gShadowPCF,
        float3(uv + float2( kTexelSize,  kTexelSize), (float)cascadeIndex), depth);
    return shadow * 0.25f;
}

// Select the tightest cascade that contains pixelDepth (view-space linear depth).
// Returns 1.0 when shadows are disabled or the pixel lies beyond all cascades.
float EvaluateDirectionalShadow(float3 worldPos, float pixelViewDepth) {
    if (!gPerFrame.hasShadows)
        return 1.0f;

    // Find tightest cascade (splits stored as view-space distances).
    uint cascade = 3u;
    [unroll]
    for (uint c = 0; c < 4; ++c) {
        if (pixelViewDepth < gPerFrame.cascadeSplits[c]) {
            cascade = c;
            break;
        }
    }

    float4 shadowCoord = mul(float4(worldPos, 1.0f), gPerFrame.shadowCascadeMat[cascade]);
    return SampleShadowCascade(shadowCoord, cascade);
}

struct PSInput {
    float4 svPos     : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 worldNorm : TEXCOORD1;
    float4 worldTan  : TEXCOORD2;   // xyz = tangent, w = bitangent sign
    float2 uv        : TEXCOORD3;
    uint   matIdx    : TEXCOORD4;
};

// Cook-Torrance GGX normal distribution function.
float D_GGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Smith-Schlick-GGX geometric shadowing.
float G_Smith(float NdotV, float NdotL, float roughness) {
    float r  = roughness + 1.0f;
    float k  = (r * r) / 8.0f;
    float gV = NdotV / (NdotV * (1.0f - k) + k);
    float gL = NdotL / (NdotL * (1.0f - k) + k);
    return gV * gL;
}

// Schlick Fresnel approximation.
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Fresnel with roughness remapping — used for IBL ambient.
// max(1-roughness, F0) replaces the 1 in standard Schlick so that
// rough surfaces retain the correct F0 floor at glancing angles.
float3 F_Schlick_Roughness(float cosTheta, float3 F0, float roughness) {
    float3 one      = float3(1.0f, 1.0f, 1.0f);
    float3 oneMinusR = max(one - roughness, F0);
    return F0 + (oneMinusR - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float4 main(PSInput IN) : SV_Target {
    GpuMaterial mat = gMaterials[IN.matIdx];

    // --- Base color ---
    float4 baseColor = mat.albedoFactor;
    if (mat.albedoTextureIndex != kNoTexture)
        baseColor *= gTextures[mat.albedoTextureIndex].Sample(gSampler, IN.uv);

    // Unlit viewMode: output raw albedo (gamma-corrected), skip all PBR/lighting.
    if (gPerFrame.viewMode == 1u) {
        float3 unlit = pow(max(baseColor.rgb, 0.0f), 1.0f / 2.2f);
        return float4(unlit, baseColor.a);
    }

    // --- Metallic / roughness ---
    float metallic  = mat.metallicFactor;
    float roughness = mat.roughnessFactor;
    if (mat.metallicRoughnessIndex != kNoTexture) {
        // glTF: R = metallic, G = roughness
        float2 mr = gTextures[mat.metallicRoughnessIndex].Sample(gSampler, IN.uv).rg;
        metallic  *= mr.r;
        roughness *= mr.g;
    }
    roughness = max(roughness, 0.04f);

    // --- Normal (TBN) ---
    float3 N = normalize(IN.worldNorm);
    float3 T = normalize(IN.worldTan.xyz);
    float3 B = cross(N, T) * IN.worldTan.w;
    if (mat.normalTextureIndex != kNoTexture) {
        float3 nSample = gTextures[mat.normalTextureIndex].Sample(gSampler, IN.uv).xyz;
        nSample = nSample * 2.0f - 1.0f;
        N = normalize(nSample.x * T + nSample.y * B + nSample.z * N);
    }

    // --- View vector ---
    float3 V    = normalize(gPerFrame.cameraWorldPos - IN.worldPos);
    float  NdotV = saturate(dot(N, V));

    // --- F0 ---
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor.rgb, metallic);

    // --- Linear view-space depth (for cascade selection) ---
    // Transform world position into view space; negate Z because RH view space
    // has objects in front at negative Z, but we want a positive depth value.
    float3 viewPos      = mul(float4(IN.worldPos, 1.0f), gPerFrame.viewMatrix).xyz;
    float  pixelViewDepth = -viewPos.z;   // positive distance along the view axis

    // --- Light accumulation ---
    float3 Lo = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0; i < gPerFrame.lightCount; ++i) {
        GpuLight light = gLights[i];
        uint lightType = (uint)light.position.w;

        float3 L;
        float  attenuation  = 1.0f;
        float  shadowFactor = 1.0f;

        if (lightType == 0u) {
            // Directional: direction is the light direction (toward the light source)
            L = normalize(light.direction.xyz);
            // PCF shadow from the CSM cascade selected by view-space depth.
            shadowFactor = EvaluateDirectionalShadow(IN.worldPos, pixelViewDepth);
        } else {
            // Point / spot
            float3 toLight = light.position.xyz - IN.worldPos;
            float  dist    = length(toLight);
            L = toLight / max(dist, 0.0001f);
            // Inverse-square attenuation with range cutoff
            float range = light.direction.w;
            float atten = saturate(1.0f - (dist / range) * (dist / range));
            attenuation = atten * atten;

            if (lightType == 2u) {
                // Spot: additional cone falloff
                float cosAngle  = dot(-L, normalize(light.direction.xyz));
                float cosOuter  = light.spotAngles.x;
                float invDiff   = light.spotAngles.y;
                float spotFactor = saturate((cosAngle - cosOuter) * invDiff);
                attenuation *= spotFactor * spotFactor;
                // TODO Phase 10 R2: sample gShadowSpots using light.spotAngles.z as array index
                // when gPerFrame.hasShadows != 0 and spotAngles.z >= 0.
            }
            // TODO Phase 10 R2: point light cube-face shadow lookup via gShadowSpots.
        }

        float3 H    = normalize(V + L);
        float  NdotL = saturate(dot(N, L));
        float  NdotH = saturate(dot(N, H));
        float  HdotV = saturate(dot(H, V));

        float  D = D_GGX(NdotH, roughness);
        float  G = G_Smith(NdotV, NdotL, roughness);
        float3 F = F_Schlick(HdotV, F0);

        float3 kD       = (1.0f - F) * (1.0f - metallic);
        float3 diffuse  = kD * baseColor.rgb / PI;
        float3 specular = D * G * F / max(4.0f * NdotV * NdotL, 0.001f);

        Lo += (diffuse + specular) * light.color.rgb * NdotL * attenuation * shadowFactor;
    }

    // --- Ambient (IBL split-sum or flat fallback) ---
    float3 ambient;
    if (gPerFrame.hasIbl) {
        float3 F_ibl  = F_Schlick_Roughness(NdotV, F0, roughness);
        float3 kS_ibl = F_ibl;
        float3 kD_ibl = (1.0f - kS_ibl) * (1.0f - metallic);

        // Diffuse IBL from irradiance map (pre-integrated over hemisphere)
        float3 irradiance  = gIrradianceMap.Sample(gLinearClamp, N).rgb;
        float3 diffuseIBL  = irradiance * baseColor.rgb * kD_ibl;

        // Specular IBL from prefiltered env map + BRDF LUT (split-sum approximation)
        float3 R = reflect(-V, N);
        const float MAX_REFLECTION_LOD = 6.0f;
        float3 prefilteredColor = gPrefilteredEnv.SampleLevel(
            gLinearClamp, R, roughness * MAX_REFLECTION_LOD).rgb;
        float2 brdf    = gBrdfLut.Sample(gLinearClamp, float2(NdotV, roughness)).rg;
        float3 specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);

        ambient = diffuseIBL + specularIBL;
    } else {
        ambient = float3(0.4f, 0.4f, 0.4f) * baseColor.rgb;
    }

    // --- Emissive ---
    float3 emissive = mat.emissiveFactor;
    if (mat.emissiveTextureIndex != kNoTexture)
        emissive *= gTextures[mat.emissiveTextureIndex].Sample(gSampler, IN.uv).rgb;

    float3 color = ambient + Lo + emissive;

    // Reinhard tonemapping + gamma correction.
    color = color / (color + 1.0f);
    color = pow(max(color, 0.0f), 1.0f / 2.2f);

    return float4(color, baseColor.a);
}
