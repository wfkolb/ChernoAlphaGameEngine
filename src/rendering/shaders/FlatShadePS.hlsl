struct PsIn {
    float4 posClip    : SV_POSITION;
    float3 worldNormal: NORMAL;
};

float4 main(PsIn p) : SV_TARGET
{
    float3 n = normalize(p.worldNormal);
    return float4(n * 0.5f + 0.5f, 1.0f);
}
