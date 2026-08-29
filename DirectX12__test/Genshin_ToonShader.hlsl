#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0); // アルベド
Texture2D g_RampTexture : register(t1); // トゥーンランプ(階調)
Texture2D g_NormalMap : register(t2);
Texture2D g_MetalMap : register(t3); // 金属マップ(体用): rgb=ハイライト色 a=金属マスク
Texture2D g_LightMap : register(t4); // 顔専用ライトマップ(rough枠を流用): r=明暗マスク
SamplerState g_Sampler : register(s0);
SamplerState g_RampSampler : register(s1);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor; // rgb: リムカラー / a: リム強度
    float4 mapFlags; // x:hasNormal y:hasMetal z:hasRough w:envMaxMip
    float4 faceParam; // x:isFace y:未使用 z:未使用 w:アウトライン幅
}

float4 Genshin_ToonPS(PSInput input) : SV_TARGET
{
    float4 texColor = g_Texture.Sample(g_Sampler, input.uv);
    float3 baseColor = input.col.rgb * texColor.rgb;
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);

    const bool isFace = faceParam.x > 0.5f;
    const float shininess = lerp(32.0f, 8.0f, saturate(roughness));

    // 金属マップ: 体材質のハイライト色/強さを制御(未設定ならmetallicスカラーで代用)
    const float4 metalSample = (mapFlags.y > 0.5f)
        ? g_MetalMap.Sample(g_Sampler, input.uv)
        : float4(1.0f, 1.0f, 1.0f, metallic);
    const float3 specTint = metalSample.rgb;
    const float specStrength = metalSample.a;

    float3 diffuse = 0;
    float specMask = 0;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        float nDotL = saturate(dot(N, L)) * atten;

        float3 ramp;
        if (isFace)
        {
            // 顔: 法線ベースの明暗ではなく、絵師が指定した固定マスクで陰影を切替える
            float mask = g_LightMap.Sample(g_Sampler, input.uv).r;
            ramp = g_RampTexture.Sample(g_RampSampler, float2(mask, 0.5f)).rgb;
        }
        else
        {
            ramp = g_RampTexture.Sample(g_RampSampler, float2(nDotL, 0.5f)).rgb;
        }
        diffuse += lights[i].color.rgb * baseColor * ramp;

        // ハイライト: smoothstepでエッジのジャギーを軽減
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), shininess);
        specMask = max(specMask, smoothstep(0.45f, 0.55f, spec) * atten);
    }

    float3 color = diffuse + baseColor * ambientColor.rgb;
    color += specMask * specStrength * specTint;

    // 顔にはリムライトを乗せない
    if (!isFace)
    {
        float rim = Fresnel(N, V, 3.0f);
        color += rimColor.rgb * rim * rimColor.a;
    }

    return float4(color, input.col.a * texColor.a);
}