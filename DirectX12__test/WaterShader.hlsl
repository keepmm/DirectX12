/*****************************************************************//**
 * \file   WaterShader.hlsl
 * \brief  水面。フレネルで「浅い角度＝鏡、真上＝透ける」を作る。
 *         波は sin の重ね合わせを解析微分して法線を求める（テクスチャ不要）
 *
 * 作成者 keeep
 * 作成日 2026/9/18
 * 更新履歴 9.18 新規作成
 *********************************************************************/
#include "Common.hlsli"
#include "Lighting.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_Env : register(t5);
Texture2D g_Reflection : register(t8);
SamplerState g_Sampler : register(s0);

#include "MaterialCB.hlsli"

static const float WATER_PI2 = 6.283185307179586f;
static const float WATER_PI = 3.141592653589793f;

// SkyBoxShader.hlsl と同じ向きに揃える
float2 WaterDirToEquirect(float3 d)
{
    return float2(atan2(d.z, d.x) / WATER_PI2 + 0.5f,
                  acos(clamp(d.y, -1.0f, 1.0f)) / WATER_PI);
}

// 波 高さ h = A * sin(dot(dir,p) * k + t * w)を四本重ねる
float3 WaveNormal(float2 p,float t)
{
    // 向き / 波長倍率 / 振幅倍率 / 向きをばらす
    const float2 dirs[4] =
    {
        float2(1.00f, 0.00f),
        float2(0.31f, 0.95f),
        float2(-0.71f, 0.71f),
        float2(0.60f, -0.80f),
    };
    const float kMul[4] = { 1.0f, 2.4f, 4.1f, 8.7f };
    const float aMul[4] = { 1.0f, 0.5f, 0.25f, 0.125f };
    
    const float baseK = WATER_PI2 / max(waveParams.y, 0.01f);
    const float speed = waveParams.z;

    float2 slope = 0.0f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const float k = basecolor * kMul[i];
        // 深水波の分残関係 w = sqrt(g * k) 短い波ほど速く走る
        const float w = sqrt(9.8f * k) * speed;
        const float a = aMul[i];
        const float ph = dot(dirs[i], p) * k + t * w;
        // d / dp の係数 振り幅は最後に waveParam.xでまとめてスケール
        slope += dirs[i] * (a * cos(ph));
    }
    
    // 振幅のスケールを最後にまとめてかける
    slope *= waveParams.x;
    
    // 高さマップ(h (x , z)の法線 = normalize(-dh / dzx , 1 -dh / dz)
    return normalize(float3(-slope.x, 1.0f, -slope.y));
}

float4 WaterPS(PSInput input) : SV_TARGET
{
    const float t = cameraPos.w;
}