#include "../common/CommonTypes.hlsli"

// Root signature bindings:
//   b0 = PerFrameConstants  (D3D12_SHADER_VISIBILITY_ALL)
//   b1 = PerObjectConstants (D3D12_SHADER_VISIBILITY_VERTEX)
ConstantBuffer<PerFrameConstants>  gPerFrame  : register(b0);
ConstantBuffer<PerObjectConstants> gPerObject : register(b1);

// Vertex layout matching VertexStatic (28 bytes):
//   position : float3     (DXGI_FORMAT_R32G32B32_FLOAT,   offset  0)
//   normal   : R10G10B10A2_UNORM packed uint              (offset 12)
//   tangent  : R10G10B10A2_UNORM packed uint, bit31=sign  (offset 16)
//   uv       : float2                                      (offset 20)
struct VSInput {
    float3 position      : POSITION;
    uint   normalPacked  : NORMAL;    // R10G10B10A2_UNORM
    uint   tangentPacked : TANGENT;   // R10G10B10A2_UNORM, bit31 = bitangent sign
    float2 uv            : TEXCOORD0;
};

struct VSOutput {
    float4 svPos     : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float3 worldNorm : TEXCOORD1;
    float4 worldTan  : TEXCOORD2;   // xyz = tangent, w = bitangent sign
    float2 uv        : TEXCOORD3;
    uint   matIdx    : TEXCOORD4;
};

// Unpack R10G10B10A2_UNORM xyz and remap [0,1] -> [-1,1].
float3 UnpackXYZ(uint packed) {
    float x = ((packed >>  0) & 0x3FFu) / 1023.0f;
    float y = ((packed >> 10) & 0x3FFu) / 1023.0f;
    float z = ((packed >> 20) & 0x3FFu) / 1023.0f;
    return normalize(float3(x, y, z) * 2.0f - 1.0f);
}

VSOutput main(VSInput IN) {
    float4 worldPos4 = mul(float4(IN.position, 1.0f), gPerObject.worldMatrix);

    // Bitangent sign encoded in bit 31 of packedTangent: 0 -> +1, 1 -> -1.
    float bitangentSign = (IN.tangentPacked & 0x80000000u) ? -1.0f : 1.0f;

    float3 objNorm = UnpackXYZ(IN.normalPacked);
    float3 objTan  = UnpackXYZ(IN.tangentPacked);

    VSOutput OUT;
    OUT.svPos    = mul(worldPos4, gPerFrame.viewProjMatrix);
    OUT.worldPos = worldPos4.xyz;
    OUT.worldNorm = normalize(mul(float4(objNorm, 0.0f), gPerObject.worldMatrix).xyz);
    OUT.worldTan  = float4(normalize(mul(float4(objTan, 0.0f), gPerObject.worldMatrix).xyz), bitangentSign);
    OUT.uv        = IN.uv;
    OUT.matIdx    = gPerObject.materialIndex;
    return OUT;
}
