#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

// ライト方向依存リムの強さ。上げるとMVのようにシルエットが起きる
#define RIM_STRENGTH 0.9f

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
    
    // シルエット際で1に近づく。リムの土台に使う
    const float fresnel = Fresnel(N, V, 3.0f);

    float3 diffuse = 0;
    float3 rimLight = 0;
    float3 specColor = 0;
    float specMask = 0;
    const float sssStrength = sssParams.x;
    const float sssWrap = max(sssParams.y, 0.0001f);

    const int count = (int)lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);

        // ラップライティング: 明暗境界を肌らしくなだらかに
        float ndl = dot(N, L);
        float lit = saturate((ndl + sssWrap) / (1.0f + sssWrap));
        float nDotL = lerp(saturate(ndl), lit, sssStrength) * atten;

        // ランプで階調化したディフューズ(2 ~ 3トーン)
        float3 ramp = g_RampTexture.Sample(g_RampSampler, float2(nDotL, 0.5f)).rgb;

        // 影側ほど SSS 色(赤み)に寄せる = 血色
        float3 shade = lerp(1.0f, sssColor.rgb, saturate(1.0f - ramp.g) * sssStrength);
        diffuse += lights[i].color.rgb * baseColor * ramp * shade;

        // 逆光透過(耳・指・鼻先)
        float trans = pow(saturate(dot(V, -L)), 3.0f) * atten * sssParams.z;
        diffuse += lights[i].color.rgb * baseColor * sssColor.rgb * trans;

        // ライト方向に依存するリム。バックライトほど縁が強く光る
        rimLight += lights[i].color.rgb * fresnel * saturate(ndl) * atten;

        // アニメ調すぺきゅら(境界を少しだけぼかす)
        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), shininess);
        float mask = smoothstep(0.35f, 0.65f, spec) * atten;
        if (mask > specMask)
        {
            specMask = mask;
            specColor = lights[i].color.rgb;
        }
    }

    // 環境光も肌色寄りに: 真っ白な ambient だと血色が飛ぶ
    float3 amb = lerp(ambientColor.rgb, ambientColor.rgb * sssColor.rgb, 0.5f * sssParams.x);
    float3 color = diffuse + baseColor * amb;

    color += specColor * specMask * metallic;
    color += rimLight * RIM_STRENGTH;
    color += rimColor.rgb * fresnel * rimColor.a; // マテリアル指定の固定リム

    return float4(color, input.col.a * texColor.a);
}