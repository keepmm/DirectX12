#include "Common.hlsli"
#include "Lighting.hlsli"

#include "MaterialCB.hlsli"
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 BasicPS(PSInput input) : SV_TARGET
{
    float4 texcolor = g_Texture.Sample(g_Sampler, input.uv);
    clip(faceParam.y * input.col.a - 0.05f);
    float3 n = normalize(input.normal);
    float3 baseColor = input.col.rgb * texcolor.rgb * basecolor.rgb;

    float3 diffuse = 0;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 l;
        float atten;
        ComputeLight(lights[i], input.worldPos, l, atten);
        // 影響圏外のライトはここで捨てる。届かない灯まで評価すると
        // ランプ参照ぶんの負荷がそのまま灯数倍になり、暗部も灯数ぶん持ち上がる
        if (atten <= 1e-3f) continue;
        float ndotl = saturate(dot(n, l));
        diffuse += lights[i].color.rgb * baseColor * ndotl * atten;
    }
    return float4(diffuse + baseColor * ambientColor.rgb,
        input.col.a * texcolor.a * basecolor.a);
}

float4 WireFramePS(PSInput input) : SV_Target
{
    return float4(0, 0, 0, 1);
}


float4 unlitPS(PSInput input) : SV_Target
{
    float4 texcolor = g_Texture.Sample(g_Sampler, input.uv);
    clip(faceParam.y * input.col.a - 0.05f);
    return float4(texcolor.rgb * basecolor.rgb,
        texcolor.a * input.col.a * faceParam.y * basecolor.a);
}