/*****************************************************************//**
 * \file   GlassShader.hlsl
 * \brief  ガラス。フレネルで映り込みと不透明度が視線角に応じて変わる
 *
 * 作成者 keeep
 * 作成日 2026/9/11
 * 更新履歴 9.11 新規作成
 *********************************************************************/
#include "Common.hlsli"
#include "Lighting.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_Normal  : register(t2);
Texture2D g_Env     : register(t5);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags;      // x:hasNormal y:hasMetal z:hasRough w:envMaxMip
    float4 faceParam;     // y:baseAlpha
    float4 sssParams;
    float4 sssColor;
    float4 basecolor;     // rgb:ガラスの色 a:正面から見たときの不透明度
    float4 reflectParam;
    float4 pbrParams;
    float4 emissiveColor;
}

static const float GLASS_PI2 = 6.283185307179586f;
static const float GLASS_PI  = 3.141592653589793f;

// スカイボックス(SkyBoxShader.hlsl)と同じ向きに揃えること
float2 GlassDirToEquirect(float3 d)
{
    return float2(atan2(d.z, d.x) / GLASS_PI2 + 0.5f,
                  acos(clamp(d.y, -1.0f, 1.0f)) / GLASS_PI);
}

float4 GlassPS(PSInput input) : SV_TARGET
{
    float3 N = normalize(input.normal);

    if (mapFlags.x > 0.5f)
    {
        float3 T = normalize(input.tangent);
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T);
        float3 nTex = g_Normal.Sample(g_Sampler, input.uv).rgb * 2.0f - 1.0f;
        N = normalize(mul(nTex, float3x3(T, B, N)));
    }

    float3 V = normalize(cameraPos.xyz - input.worldPos);

    // 裏面を見ているときは法線を反転（ガラスは両面が見える）
    if (dot(N, V) < 0.0f) N = -N;
    float ndotv = saturate(dot(N, V));

    // ガラスの垂直反射率は 4%。浅い角度ほど鏡に近づく（Schlick）
    float F = 0.04f + 0.96f * pow(1.0f - ndotv, 5.0f);

    float3 tint = basecolor.rgb * g_Texture.Sample(g_Sampler, input.uv).rgb;

    // 映り込み
    float3 refl;
    if (mapFlags.w > 0.0f)
    {
        float maxMip = mapFlags.w;
        float3 R = reflect(-V, N);
        // 最も粗い2ミップは拡散用に潰してあるので使わない
        refl = g_Env.SampleLevel(g_Sampler, GlassDirToEquirect(R),
                                 roughness * max(maxMip - 2.0f, 0.0f)).rgb;
    }
    else
    {
        refl = ambientColor.rgb;
    }

    // 光源のハイライト（ガラスに映る灯り）
    float3 spec = 0.0f;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        if (atten <= 1e-3f) continue;

        float3 H = normalize(L + V);
        float power = exp2(lerp(11.0f, 4.0f, saturate(roughness)));
        spec += lights[i].color.rgb * pow(saturate(dot(N, H)), power) * atten;
    }

    float3 color = tint * (1.0f - F) + refl * F + spec * F * 4.0f;

    // 縁ほど不透明に見える（映り込みが強くなるぶん）
    float alpha = saturate(basecolor.a + F);

    return float4(color, alpha * faceParam.y);
}
