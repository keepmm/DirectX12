#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_RampTexture : register(t1);
SamplerState g_Sampler : register(s0);
SamplerState g_RampSampler : register(s1);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags;
    float4 faceParam; // y = baseAlpha
    float4 sssParams;
    float4 sssColor;
    float4 matBaseColor;
}

float4 ToonPS(PSInput input) : SV_TARGET
{
    float4 texColor = g_Texture.Sample(g_Sampler, input.uv);
    clip(faceParam.y * input.col.a - 0.05f); // 非表示マテリアルを消す
    float3 baseColor = input.col.rgb * texColor.rgb * matBaseColor.rgb;
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    
    float shininess = lerp(64.0f, 8.0f, saturate(roughness));
    
    float3 diffuse = 0;
    float specMask = 0;
    const int count = (int)lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        float nDotL = saturate(dot(N, L)) * atten;
        
        // ランプで階調化したディフューズ(2 ~ 3トーン)
        float3 ramp = g_RampTexture.Sample(g_RampSampler, float2(nDotL, 0.5f)).rgb;
        return float4(ramp, 1.0f);
        diffuse += lights[i].color.rgb * baseColor * ramp;
        
        // アニメ調すぺきゅら
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), shininess);
        specMask = max(specMask, step(0.5f, spec) * atten);

    }

    float3 color = diffuse + baseColor * ambientColor.rgb;
    color += specMask * metallic;
    
    float rim = Fresnel(N, V, 3.0f);
    color += rimColor.rgb * rim * rimColor.a;
    
    return float4(color, input.col.a * texColor.a);
}