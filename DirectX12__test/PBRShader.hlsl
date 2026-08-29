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
    float4 faceParam; // レイアウト合わせ(未使用)
    float4 sssParams; // x=SSS強度 y=ラップ z=透過 w=布シーン
    float4 sssColor;
}

float4 PbrPS(PSInput input) : SV_TARGET
{
    float4 texSample = g_Texture.Sample(g_Sampler, input.uv);
    clip(faceParam.y  - 0.05f);
    float3 albedo = input.col.rgb * texSample.rgb;

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
        float s = (i == 0) ? shadow : 1.0f;

        float3 direct;
        
        // ---- 布 ---- //
        if(sssParams.w > 0.0f)
        {
            float3 sheenCol = sssParams.w * float3(1.0f, 1.0f, 1.0f);
            direct = ClothBRDF(albedo, sheenCol, r, N, V, L, lights[i].color.rgb);
        }
        else
        {
            direct = CookTorrance(albedo, m, r, N, V, L, lights[i].color.rgb);
        }

        // --- 肌: ラップライティングによる疑似SSS ---
        if (sssParams.x > 0.0f)
        {
            float w = sssParams.y;
            float wrap = saturate((dot(N, L) + w) / (1.0f + w));
            // 明暗境界だけ散乱色に染める
            float scatter = wrap * (1.0f - wrap) * 4.0f;
            float3 sssDiff = albedo * lights[i].color.rgb
                           * (wrap + scatter * sssColor.rgb);

            // 逆光透過(耳・指先が赤く抜ける)
            float3 backL = normalize(-L + N * 0.3f);
            float trans = pow(saturate(dot(V, backL)), 4.0f) * sssParams.z;

            direct = lerp(direct, sssDiff, sssParams.x)
                   + albedo * sssColor.rgb * trans * lights[i].color.rgb;
        }

        // --- 布(靴下): Charlie近似シーン(起毛のハイライト) ---
        if (sssParams.w > 0.0f)
        {
            float2 fuv = input.uv * 400.0f; // 繊維の細かさ
            float n1 = frac(sin(dot(floor(fuv), float2(12.9898f, 78.233f))) * 43758.5453f);
            float n2 = frac(sin(dot(floor(fuv.yx), float2(39.3468f, 11.135f))) * 24634.6345f);
            float3 T2 = normalize(input.tangent - N * dot(N, input.tangent));
            float3 B2 = cross(N, T2);
            N = normalize(N + (T2 * (n1 - 0.5f) + B2 * (n2 - 0.5f)) * 0.15f); // 0.15=繊維の粗さ
        }

        color += direct * atten * s;
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