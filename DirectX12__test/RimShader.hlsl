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
    float4 mapFlags;
    float4 faceParam;
    float4 rimParam; // x: width, y: softness, z: lightMask, w: baseColor tint
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
    float3 rimLight = 0; // light color that drives the rim
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], pin.worldPos, L, atten);
        diffuse += Lambert(basecolor, lights[i].color.rgb, N, L) * atten;
        rimLight += lights[i].color.rgb * atten * RimLightMask(N, L, rimParam.z);
    }

    float3 finalColor = diffuse + basecolor * ambientColor.rgb;
    
    // リムを加算
    float rim = RimBand(N, V, rimParam.x, rimParam.y);
    float3 rimTint = lerp(rimColor.rgb, rimColor.rgb * basecolor, saturate(rimParam.w));
    finalColor += rimTint * rim * rimColor.a * (rimLight + ambientColor.rgb);
    
    return float4(finalColor, pin.col.a * color.a);

}