#include "Common.hlsli"

struct BeamVSInput
{
    float3 pos : POSITION;
    float4 col : COLOR; // rgb: ビームの色, a: 芯からの距離係数(1=中心, 0=縁)
};

struct BeamVSOutput
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

BeamVSOutput LaserVS(BeamVSInput input)
{
    BeamVSOutput o;
    o.pos = mul(float4(input.pos, 1.0f), viewProj);
    o.col = input.col;
    return o;
}

float4 LaserPS(BeamVSOutput input) : SV_TARGET
{
    // 中心ほど明るく、縁は滑らかに消えるグロー表現
    const float core = pow(saturate(input.col.a), 3.0f);
    return float4(input.col.rgb * core, core);
}

float4 FireworkPS(BeamVSOutput input) : SV_TARGET
{
    const float core = input.col.a * input.col.a; // 2乗で丸く柔らかい玉に
    return float4(input.col.rgb * core, 1.0f);
}