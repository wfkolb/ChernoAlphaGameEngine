#include "../common/CommonTypes.hlsli"

ConstantBuffer<PerFrameConstants> gPerFrame : register(b0);

// Bindless structured buffer of all materials at t0, space0.
StructuredBuffer<GpuMaterial> gMaterials : register(t0, space0);
// Bindless texture array at t1+, space1.
Texture2D gTextures[] : register(t1, space1);

SamplerState gSampler : register(s0);  // LinearWrap

static const float PI = 3.14159265f;
static const uint  kNoTexture = 0xFFFFFFFFu;

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

float4 main(PSInput IN) : SV_Target {
    GpuMaterial mat = gMaterials[IN.matIdx];

    // --- Base color ---
    float4 baseColor = mat.albedoFactor;
    if (mat.albedoTextureIndex != kNoTexture)
        baseColor *= gTextures[mat.albedoTextureIndex].Sample(gSampler, IN.uv);

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

    // --- Single directional light (placeholder; real lights go in a light buffer) ---
    float3 L    = normalize(float3(0.5f, 1.0f, 0.3f));
    float3 H    = normalize(V + L);
    float  NdotL = saturate(dot(N, L));
    float  NdotH = saturate(dot(N, H));
    float  HdotV = saturate(dot(H, V));

    float  D = D_GGX(NdotH, roughness);
    float  G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(HdotV, F0);

    float3 kD      = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kD * baseColor.rgb / PI;
    float3 specular = D * G * F / max(4.0f * NdotV * NdotL, 0.001f);

    float3 lightColor = float3(1.0f, 1.0f, 1.0f) * 3.0f;
    float3 Lo = (diffuse + specular) * lightColor * NdotL;

    // --- Ambient ---
    float3 ambient = float3(0.03f, 0.03f, 0.03f) * baseColor.rgb;

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
