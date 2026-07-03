#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_Normal : register(t2);
Texture2D g_Metal : register(t3);
Texture2D g_Rough : register(t4);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags; // x=hasNormal, y=hasMetal, z=hasRough
}

float4 PbrPS(PSInput input) : SV_TARGET
{
    float3 albedo = input.col.rgb * g_Texture.Sample(g_Sampler, input.uv).rgb;

    // 法線マップ（あれば適用）
    float3 N = normalize(input.normal);
    if (mapFlags.x > 0.5f)
    {
        float3 T = normalize(input.tangent);
        T = normalize(T - N * dot(N, T)); // グラム・シュミット直交化
        float3 B = cross(N, T);
        float3 nTex = g_Normal.Sample(g_Sampler, input.uv).rgb * 2.0f - 1.0f;
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(nTex, TBN));
    }

    // metal / rough（あればテクスチャ優先）
    float m = (mapFlags.y > 0.5f) ? g_Metal.Sample(g_Sampler, input.uv).r : metallic;
    float r = (mapFlags.z > 0.5f) ? g_Rough.Sample(g_Sampler, input.uv).r : roughness;

    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float3 color = 0;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        color += CookTorrance(albedo, m, r, N, V, L, lights[i].color.rgb) * atten;
    }
    color += albedo * ambientColor.rgb;
    return float4(color, input.col.a);
}