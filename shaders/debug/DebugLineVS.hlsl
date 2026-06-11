// Debug line vertex shader — unlit world-space line list.
// Vertex: float xyz[3] + uint32_t packedColor (R8G8B8A8_UNORM).
cbuffer PerFrame : register(b0) {
    row_major float4x4 gViewProj;
};

struct VSIn {
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VSOut {
    float4 pos   : SV_POSITION;
    float4 color : COLOR;
};

VSOut main(VSIn v) {
    VSOut o;
    o.pos   = mul(float4(v.position, 1.0f), gViewProj);
    o.color = v.color;
    return o;
}
