// Skinned mesh vertex shader — identical to OpaqueVS plus bone transform lookup.
cbuffer PerDraw : register(b0) {
    row_major float4x4 gMVP;
};

// Bone palette — up to 256 bones, per draw call.
StructuredBuffer<row_major float4x4> gBones : register(t0);

struct VSIn {
    float3 position     : POSITION;
    uint   packedNormal : NORMAL;
    uint   packedTangent: TANGENT;
    float2 uv           : TEXCOORD0;
    uint4  boneIndices  : BLENDINDICES;
    float4 boneWeights  : BLENDWEIGHT;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(VSIn v) {
    // Reconstruct skinning matrix.
    float4x4 skinMat = gBones[v.boneIndices.x] * v.boneWeights.x
                     + gBones[v.boneIndices.y] * v.boneWeights.y
                     + gBones[v.boneIndices.z] * v.boneWeights.z
                     + gBones[v.boneIndices.w] * v.boneWeights.w;

    float4 worldPos = mul(float4(v.position, 1.0f), skinMat);
    VSOut o;
    o.pos = mul(worldPos, gMVP);
    o.uv  = v.uv;
    return o;
}
