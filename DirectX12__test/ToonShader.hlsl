#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_RampTexture : register(t1);
SamplerState g_Sampler : register(s0);
SamplerState g_RampSampler : register(s1);

cbuffer Material : register(b3)
{
    float roughness;    // ハイライトの大きさ（大=広い）
    float metallic;     // ハイライトの強さ
    float2 _pad;
    float4 rimColor;    // rgb: リム色 / a: リム強さ
    float4 mapFlags;
    float4 faceParam;
    float4 rimParam; // x: width, y: softness, z: lightMask, w: baseColor tint
}

float4 ToonPS(PSInput input) : SV_TARGET
{
    float4 texColor = g_Texture.Sample(g_Sampler, input.uv);
    float3 baseColor = input.col.rgb * texColor.rgb;
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    
    float shininess = lerp(64.0f, 8.0f, saturate(roughness));
    
    float3 diffuse = 0;
    float specMask = 0;
    float3 rimLight = 0; // light color that drives the rim
    const int count = (int)lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        float nDotL = saturate(dot(N, L)) * atten;
        
        // ランプで階調化したディフューズ(2 ~ 3トーン)
        float3 ramp = g_RampTexture.Sample(g_RampSampler, float2(nDotL, 0.5f)).rgb;
        diffuse += lights[i].color.rgb * baseColor * ramp;
        
        // アニメ調すぺきゅら
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), shininess);
        specMask = max(specMask, step(0.5f, spec) * atten);

        // the rim follows the lights instead of being a constant white outline
        rimLight += lights[i].color.rgb * atten * RimLightMask(N, L, rimParam.z);

    }

    float3 color = diffuse + baseColor * ambientColor.rgb;
    color += specMask * metallic;
    
    float rim = RimBand(N, V, rimParam.x, rimParam.y);
    float3 rimTint = lerp(rimColor.rgb, rimColor.rgb * baseColor, saturate(rimParam.w));
    color += rimTint * rim * rimColor.a * (rimLight + ambientColor.rgb);
    
    return float4(color, input.col.a * texColor.a);
}