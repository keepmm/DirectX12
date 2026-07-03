#include "Common.hlsli"

PSInput BasicVS(VSInput input)
{
    PSInput output;
    float4 wp = mul(float4(input.pos, 1.0f), world);
    output.pos = mul(wp, viewProj);
    output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    output.worldPos = wp.xyz;
    output.col = input.col;
    output.uv = input.uv;
    output.tangent = normalize(mul(float4(input.tangent, 0.0f), world).xyz);
    return output;
}