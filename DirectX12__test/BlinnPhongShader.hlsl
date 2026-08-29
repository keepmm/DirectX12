#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;    // 大 = 広く鈍い / 小 = 狭く鋭い
    float metallic;     // 鏡面の強さ
    float2 _pad;
    float4 rimColor;
}

float4 PhongPS(PSInput pin) : SV_Target
{
    // 通常のLambertライティング
    float4 tex = g_Texture.Sample(g_Sampler, pin.uv);
    float3 baseColor = pin.col.rgb * tex.rgb;
    float3 N = normalize(pin.normal);
    float3 V = normalize(cameraPos.xyz - pin.worldPos);
    
    float shininess = lerp(128.0f, 4.0f, saturate(roughness));
    
    float3 diffuse = 0;
    float3 specular = 0;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], pin.worldPos, L, atten);

        diffuse += Lambert(baseColor, lights[i].color.rgb, N, L) * atten;
        specular += BlinnPhongSpec(N, L, V, lights[i].color.rgb, shininess) * atten;
    }

    float3 color = diffuse
                 + baseColor * ambientColor.rgb
                 + specular * metallic; // 鏡面の強さ

    return float4(color, pin.col.a * tex.a);
}