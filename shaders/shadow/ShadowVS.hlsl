#include "../common/CommonTypes.hlsli"

ConstantBuffer<PerFrameConstants>  gFrame  : register(b0);
ConstantBuffer<PerObjectConstants> gObject : register(b1);

struct VSInput {
    float3 position : POSITION;
};

// Depth-only shadow pass.
// For v1: the cascade matrix to use is baked into gFrame.shadowCascadeMat[0].
// The CPU rotates through cascades 0..3 by uploading a different per-frame CB
// for each cascade draw call (4 passes per directional shadow caster).
// TODO Phase 10 R2: upgrade to a geometry-shader or instanced approach that
// renders all 4 cascades in a single draw call.
float4 main(VSInput IN) : SV_Position {
    float4 worldPos = mul(float4(IN.position, 1.0f), gObject.worldMatrix);
    return mul(worldPos, gFrame.shadowCascadeMat[0]);
}
