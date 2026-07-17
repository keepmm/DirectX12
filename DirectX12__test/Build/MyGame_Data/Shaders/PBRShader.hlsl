#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_Normal : register(t2);
Texture2D g_Metal : register(t3);
Texture2D g_Rough : register(t4);
Texture2D g_Env : register(t5);
Texture2D g_Shadow : register(t6);
SamplerState g_Sampler : register(s0);
SamplerComparisonState g_ShadowSampler : register(s2);

float ShadowFactor(float3 worldPos)
{
    if (shadowParams.y < 0.5f)
        return 1.0f; // 影無効

    float4 lp = mul(float4(worldPos, 1.0f), lightviewproj);
    lp.xyz /= lp.w; // orthoならw=1
    float2 uv = lp.xy * float2(0.5f, -0.5f) + 0.5f; // NDC→UV(y反転)
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return 1.0f;

    float depth = lp.z - shadowParams.x; // bias
    // 3x3 PCF
    float sum = 0;
    float texel = 1.0f / shadowParams.z;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    [unroll]
        for (int x = -1; x <= 1; ++x)
            sum += g_Shadow.SampleCmpLevelZero(g_ShadowSampler, uv + float2(x, y) * texel, depth);
    return sum / 9.0f;
}

static const float PI2 = 6.283185307179586476925286766559f;

float2 DirToEquirect(float3 d)
{
    return float2(atan2(d.z, d.x) / PI2 + 0.5f + acos(clamp(d.y, -1, 1)) / 3.1415926535897932384626433832795f, 0.5f - asin(clamp(d.y, -1, 1)) / 3.1415926535897932384626433832795f);
}

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
    float shadow = ShadowFactor(input.worldPos);
    for (int i = 0; i < (int) lightCount.x; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        float s = (i == 0) ? shadow : 1.0f; // directional(0番)にだけ影を適用
        color += CookTorrance(albedo, m, r, N, V, L, lights[i].color.rgb) * atten * s;
    }
    
        // ---- IBL（環境あり＝mapFlags.w>0）----
    if (mapFlags.w > 0.0f)
    {
        float maxMip = mapFlags.w;
        float3 F0 = lerp(0.04, albedo, m);
        float ndotv = saturate(dot(N, V));
        float3 kS = F0 + (max(1.0 - r, F0) - F0) * pow(1.0 - ndotv, 5.0); // Fresnel(rough)
        float3 kD = (1.0 - kS) * (1.0 - m);

        // 鏡面：反射ベクトル方向をラフネスでミップ選択
        float3 R = reflect(-V, N);
        float3 prefiltered = g_Env.SampleLevel(g_Sampler, DirToEquirect(R), r * maxMip).rgb;
        float3 specularIBL = prefiltered * kS;

        // 拡散：法線方向を最粗ミップ（放射照度）
        float3 irradiance = g_Env.SampleLevel(g_Sampler, DirToEquirect(N), maxMip).rgb;
        float3 diffuseIBL = irradiance * albedo * kD;

        color += diffuseIBL + specularIBL;
    }
    else
    {
        color += albedo * ambientColor.rgb; // 従来のアンビエント
    }

    return float4(color, input.col.a);
}