cbuffer ObjectCB : register(b0)
{
    row_major float4x4 gMVP;
};

struct VsIn {
    float3 position : POSITION;
    float4 normal   : NORMAL;    // R10G10B10A2_UNORM decoded to [0,1] by hardware
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
};

struct VsOut {
    float4 posClip    : SV_POSITION;
    float3 worldNormal: NORMAL;
};

VsOut main(VsIn v)
{
    VsOut o;
    o.posClip     = mul(float4(v.position, 1.0f), gMVP);
    o.worldNormal = normalize(v.normal.xyz * 2.0f - 1.0f);
    return o;
}
