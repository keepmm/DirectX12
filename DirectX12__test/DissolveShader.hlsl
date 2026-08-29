#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;    // ノイズの細かさ
    float metallic;     // ディソルブ量 0 = 完全, 1 = 消滅
    float2 _pad;
    float4 rimColor;    // rgb : 解け際の発行色 / a 発光の幅
}

float hash13(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.3f);
    return frac((p.x + p.y) * p.z);
}

float4 DissolvePS(PSInput pin) : SV_Target
{
    float scale = lerp(2.0f, 30.0f, saturate(roughness));
    float noise = hash13(floor(pin.worldPos * scale));
    
    // しきいち以下は破棄
    if(noise < metallic)
    {
        discard;
    }
    
    // 解け際だけ発光させる
    float width = max(rimColor.a, 0.0001f);
    float edge = saturate((noise - metallic) / width);
    float3 edgeGlow = rimColor.rgb * (1.0f - edge);
    
    // 通常のLambertライティング
    float4 tex = g_Texture.Sample(g_Sampler, pin.uv);
    float3 baseColor = pin.col.rgb * tex.rgb;
    float3 N = normalize(pin.normal);
    
    float3 diffuse = 0;
    const int count = (int) lightCount.x;
    for(int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], pin.worldPos, L, atten);
        diffuse += Lambert(baseColor, lights[i].color.rgb, N, L) * atten;
    }
    float3 lit = diffuse + baseColor * ambientColor.rgb;
    
    return float4(lit + edgeGlow, pin.col.a * tex.a);
}