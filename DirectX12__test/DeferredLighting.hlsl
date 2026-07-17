#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Albedo : register(t0);
Texture2D g_Normal : register(t2);
Texture2D g_ORM : register(t3);
Texture2D g_Depth : register(t4);
Texture2D g_Env : register(t5);
Texture2D g_Shadow : register(t6);
SamplerState g_Sampler : register(s0);
SamplerComparisonState g_ShadowSampler : register(s2);

cbuffer DeferredCB : register(b3)
{
    float4x4 invViewProj;
    float4 envParam; // x: envMaxMip（0なら環境なし）
}

//float ShadowFactor(float3 worldPos)
//{
//    if (shadowParams.y < 0.5f)
//        return 1.0f; // 影無効

//    float4 lp = mul(float4(worldPos, 1.0f), lightviewproj);
//    lp.xyz /= lp.w; // orthoならw=1
//    float2 uv = lp.xy * float2(0.5f, -0.5f) + 0.5f; // NDC→UV(y反転)
//    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
//        return 1.0f;

//    float depth = lp.z - shadowParams.x; // bias
//    // 3x3 PCF
//    float sum = 0;
//    float texel = 1.0f / shadowParams.z;
//    [unroll]
//    for (int y = -1; y <= 1; ++y)
//    [unroll]
//        for (int x = -1; x <= 1; ++x)
//            sum += g_Shadow.SampleCmpLevelZero(g_ShadowSampler, uv + float2(x, y) * texel, depth);
//    return sum / 9.0f;
//}

// 安価なシャドウ
float ShadowFactor1(float3 worldPos)
{
    if (shadowParams.y < 0.5f)
        return 1.0f;
    float4 lp = mul(float4(worldPos, 1.0f), lightviewproj);
    lp.xyz /= lp.w;
    float2 uv = lp.xy * float2(0.5f, -0.5f) + 0.5f;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1)
        return 1.0f;
    float depth = lp.z - shadowParams.x;
    return g_Shadow.SampleCmpLevelZero(g_ShadowSampler, uv, depth);
}

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOut FullScreenVS(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

static const float PI2 = 6.283185307f;

float2 DirToEquirect(float3 d)
{
    return float2(atan2(d.z, d.x) / PI2 + 0.5f + acos(clamp(d.y, -1, 1)) / PI,
                  0.5f - asin(clamp(d.y, -1, 1)) / PI);
}

float3 ToonRamp(float nDotL)
{
    float t = smoothstep(0.35f, 0.55f, nDotL);
    return lerp(0.45f.xxx, 1.0f.xxx, t);
}

float4 DeferredPS(VSOut input) : SV_TARGET
{
    float depth = g_Depth.Sample(g_Sampler, input.uv).r;
    if (depth >= 1.0f)
        discard;

    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    float4 wp = mul(float4(ndc, depth, 1.0f), invViewProj);
    float3 worldPos = wp.xyz / wp.w;

    float3 baseColor = g_Albedo.Sample(g_Sampler, input.uv).rgb;
    float3 N = g_Normal.Sample(g_Sampler, input.uv).rgb * 2.0f - 1.0f;
    float2 mr = g_ORM.Sample(g_Sampler, input.uv).rg;
    float metallic = mr.x;
    float roughness = mr.y;

    float3 V = normalize(cameraPos.xyz - worldPos);
    float shininess = lerp(64.0f, 8.0f, saturate(roughness));

    // ---- Toon 直接光 ----
    float3 diffuse = 0;
    float specMask = 0;
    float shadow = ShadowFactor1(worldPos);
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], worldPos, L, atten);
        float nDotL = saturate(dot(N, L)) * atten;

        float s = (i == 0) ? shadow : 1.0f;

        const int t = (int) lights[i].param.x;
        float rampScale = (t == 0) ? 1.0f : atten;

        diffuse += lights[i].color.rgb * baseColor * ToonRamp(nDotL) * rampScale * s;

        float3 H = normalize(L + V);
        float spec = pow(saturate(dot(N, H)), shininess);
        specMask = max(specMask, step(0.5f, spec) * atten * s);
    }

    float3 color = diffuse;
    color += specMask * metallic;

    // ---- IBL -----
    float maxMip = envParam.x;
    if (maxMip > 0.0f)
    {
        // 拡散：法線方向の最も粗いミップ＝環境の平均照度
        float3 irradiance = g_Env.SampleLevel(g_Sampler, DirToEquirect(N), maxMip).rgb;
        float3 diffuseIBL = irradiance * baseColor * envParam.y;

        // 鏡面：反射方向をラフネスでミップ選択（メタルほど強く）
        float3 R = reflect(-V, N);
        float3 prefiltered = g_Env.SampleLevel(g_Sampler, DirToEquirect(R), roughness * maxMip).rgb;
        float3 specularIBL = prefiltered * metallic;

        color += diffuseIBL + specularIBL;
    }
    else
    {
        color += baseColor * ambientColor.rgb;
    }

    return float4(color, 1.0f);
}

// Henyey-Greenstein 位相関数（前方散乱でビームがカメラ向きに強く光る）
float PhaseHG(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f));
}

// 疑似乱数（バンディング低減のディザ）
float Hash(float2 p)
{
    return frac(sin(dot(p, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float4 VolumetricPS(VSOut input) : SV_TARGET
{
    // レイの終点：深度がある面まで／空なら遠クリップ相当
    float depth = g_Depth.Sample(g_Sampler, input.uv).r;
    float2 ndc = input.uv * float2(2, -2) + float2(-1, 1);
    float4 wp = mul(float4(ndc, depth, 1.0f), invViewProj);
    float3 endPos = wp.xyz / wp.w;

    float3 camPos = cameraPos.xyz;
    float3 rayVec = endPos - camPos;
    float rayLen = length(rayVec);
    float3 rayDir = rayVec / max(rayLen, 1e-4f);

    // 遠すぎるビームは適度に打ち切る（空ピクセルの暴走防止）
    const float maxDist = 60.0f;
    rayLen = min(rayLen, maxDist);

    const int STEPS = 24;
    float stepLen = rayLen / STEPS;

    // ディザで開始位置をずらしてバンディングを消す
    float jitter = Hash(input.uv * shadowParams.z);
    float3 p = camPos + rayDir * stepLen * jitter;

    const float g = 0.3f; // 前方散乱の鋭さ(横から見ても筋が見えるように0.6→0.3)

    float3 scatter = 0;
    const int count = (int) lightCount.x;

    [loop]
    for (int i = 0; i < count; ++i)
    {
        float density = lights[i].param.w;
        int type = (int) lights[i].param.x;
        if(density <= 0.0f || type < 2)
            continue;
        
        float3 lightScatter = 0;
        
        [loop]
        for (int s = 0; s < STEPS; ++s)
        {
            float3 L;
            float atten;
            ComputeLight(lights[i], p, L, atten);
            if(atten > 0.0f)
            {
                float sh = (i == 0) ? ShadowFactor1(p) : 1.0f;
                float phase = PhaseHG(dot(-rayDir, L), g);
                lightScatter += atten * phase * sh;
            }
            p += rayDir * stepLen;
        }
        scatter += lights[i].color.rgb * lightScatter * density;
    }

    scatter *= 0.5f * stepLen; // ベース係数(density はライトごとに乗算済み)
    return float4(scatter, 1.0f);
}