#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
}

float4 RimPS(PSInput pin) : SV_Target
{
    float4 color = g_Texture.Sample(g_Sampler, pin.uv);
    float3 basecolor = pin.col.rgb * color.rgb;
    
    // 法線を正規化
    float3 N = normalize(pin.normal);
    // ビュー方向を計算
    float3 V = normalize(cameraPos.xyz - pin.worldPos);
    
    // 通常のLambertライティング
    float3 diffuse = 0;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], pin.worldPos, L, atten);
        diffuse += Lambert(basecolor, lights[i].color.rgb, N, L) * atten;
    }

    float3 finalColor = diffuse + basecolor * ambientColor.rgb;
    
    // リムを加算
    finalColor += Rim(N, V, rimColor.rgb, 4.0) * rimColor.a;
    
    return float4(finalColor, pin.col.a * color.a);

}