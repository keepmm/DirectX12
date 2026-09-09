#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

// ---- 見た目の調整値 ----
#define RIM_STRENGTH  0.8f    // ライト方向依存リムの強さ
#define RIM_POWER     2.0f    // 小さいほどリムが太くなる
#define SHADOW_SAT    1.4f    // 影の彩度。1.0で据え置き
#define SHADOW_LIFT   0.06f   // 影が黒く潰れないための下限
#define HAIR_SHIFT    0.30f   // 天使の輪の位置(法線方向へのずらし)
#define HAIR_EXP      64.0f   // 輪の細さ
#define FACE_FLATTEN  0.40f   // 顔の影の弱め具合(上げすぎると顔だけ白飛びする)
#define LIGHT_KNEE    1.00f   // 多灯の白飛びを抑える圧縮量。1.0で上限がちょうど1になる

// ---- 空気感(距離フォグ) ----
// 奥のものほど環境色に沈めて、手前のキャラを浮かび上がらせる
#define FOG_START     10.0f   // ここから霞み始める(カメラからの距離)
#define FOG_END       55.0f   // ここで最大濃度
#define FOG_DENSITY   0.65f   // 最大でどれだけ霞ませるか。0で無効
#define FOG_TOP       14.0f   // この高さから上は薄くする(床側にもやを溜める)
#define FOG_FALLOFF   16.0f   // 上へ抜けるまでの距離
#define FOG_TINT      3.0f    // フォグ色 = ambientColor * これ

Texture2D g_Texture : register(t0);
Texture2D g_Shadow : register(t6);
SamplerComparisonState g_ShadowSampler : register(s2);
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
    float4 faceParam; // x = isFace, y = baseAlpha
    float4 sssParams;
    float4 sssColor;
    float4 matBaseColor;
}

// シャドウマップの遮蔽率。1で日向、0で影
float ShadowFactor(float3 worldPos)
{
    if (shadowParams.y < 0.5f) return 1.0f;   // 影が無効

    float4 lp = mul(float4(worldPos, 1.0f), lightviewproj);
    lp.xyz /= lp.w;
    float2 uv = lp.xy * float2(0.5f, -0.5f) + 0.5f;   // NDC -> UV(y反転)
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return 1.0f;

    const float depth = lp.z - shadowParams.x;   // bias
    const float texel = 1.0f / shadowParams.z;

    // 3x3 PCF(輪郭を少しぼかす)
    float sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            sum += g_Shadow.SampleCmpLevelZero(
                g_ShadowSampler, uv + float2(x, y) * texel, depth);
        }
    }
    return sum / 9.0f;
}

// 多灯を素直に足すと白飛びするので、明るい側だけ緩やかに圧縮する
float3 SoftClip(float3 c, float knee)
{
    return c / (1.0f + c * knee);
}

// 輝度を保ったまま彩度を上げる。影を紫のまま濃くするのに使う
float3 Saturate3(float3 c, float amount)
{
    const float l = dot(c, float3(0.299f, 0.587f, 0.114f));
    return max(lerp(l.xxx, c, amount), 0.0f);
}

float4 ToonPS(PSInput input) : SV_TARGET
{
    float4 texColor = g_Texture.Sample(g_Sampler, input.uv);
    clip(faceParam.y * input.col.a - 0.05f); // 非表示マテリアルを消す

    float3 baseColor = input.col.rgb * texColor.rgb * matBaseColor.rgb;
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPos.xyz - input.worldPos);
    float3 T = normalize(input.tangent);

    const float faceFlat = faceParam.x * FACE_FLATTEN; // 顔マテリアルだけ効く
    const float hairSpec = sssParams.w;                // 髪の異方性ハイライト強度
    const float shininess = lerp(64.0f, 8.0f, saturate(roughness));
    const float fresnel = Fresnel(N, V, RIM_POWER);

    const float sssStrength = sssParams.x;
    const float sssWrap = max(sssParams.y, 0.0001f);

    float3 diffuse = 0;
    float3 rimLight = 0;
    float3 specular = 0;

    // 影を落とすのは shadowParams.w 番のライトだけ(方向ライト/スポットどちらでも)
    const float shadowMask = ShadowFactor(input.worldPos);
    const int shadowLight = (int)shadowParams.w;

    const int count = (int)lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        // 影響圏外のライトはここで捨てる。届かない灯まで評価すると
        // ランプ参照ぶんの負荷がそのまま灯数倍になり、暗部も灯数ぶん持ち上がる
        if (atten <= 1e-3f) continue;
        const float3 lc = lights[i].color.rgb;
        const float shadow = (i == shadowLight) ? shadowMask : 1.0f;

        float ndl = dot(N, L);

        // ラップライティング: 明暗境界を肌らしくなだらかに
        float lit = saturate((ndl + sssWrap) / (1.0f + sssWrap));
        float nDotL = lerp(saturate(ndl), lit, sssStrength);

        // 顔は影の落ち方だけ和らげる。明るさを上げすぎると、多灯のステージでは
        // 顔だけ真っ白に飽和するので持ち上げ幅は控えめにする
        nDotL = lerp(nDotL, saturate(nDotL * 0.7f + 0.3f), faceFlat);
        nDotL *= atten;

        // ランプで階調化したディフューズ(2 ~ 3トーン)
        float3 ramp = g_RampTexture.Sample(g_RampSampler, float2(nDotL, 0.5f)).rgb;

        // 影側ほど SSS 色(赤み)に寄せる = 血色
        float3 shade = lerp(1.0f, sssColor.rgb, saturate(1.0f - ramp.g) * sssStrength);
        diffuse += lc * baseColor * ramp * shade * shadow;

        // 逆光透過(耳・指・鼻先)
        float trans = pow(saturate(dot(V, -L)), 3.0f) * atten * sssParams.z;
        diffuse += lc * baseColor * sssColor.rgb * trans;

        // 逆光ほど強いリム。加算だと灯数ぶん白くなるので一番強いライトを採用する
        rimLight = max(rimLight, lc * fresnel * saturate(ndl) * atten);

        float3 H = normalize(L + V);
        if (hairSpec > 0.0f)
        {
            // 髪: Kajiya-Kay の異方性ハイライト。接線を法線方向へずらして輪を上げる
            float3 Ts = normalize(T + N * HAIR_SHIFT);
            float tdoth = dot(Ts, H);
            float sinTH = sqrt(saturate(1.0f - tdoth * tdoth));
            specular = max(specular, lc * pow(sinTH, HAIR_EXP) * hairSpec * atten);
        }
        else
        {
            // アニメ調すぺきゅら(境界を少しだけぼかす)
            float spec = pow(saturate(dot(N, H)), shininess);
            specular = max(specular, lc * smoothstep(0.35f, 0.65f, spec) * metallic * atten);
        }
    }

    // 影は黒ではなく環境色。彩度を上げて紫を残す
    float3 amb = lerp(ambientColor.rgb, ambientColor.rgb * sssColor.rgb, 0.5f * sssStrength);
    float3 shadowCol = Saturate3(baseColor * amb, SHADOW_SAT) + SHADOW_LIFT * amb;

    // 直接光は圧縮してから合成する。ステージの灯数が増えても飛ばないようにする
    // リムも圧縮の内側に入れる。外で足すと、圧縮しても結局1を超えて白く飛ぶ
    const float3 direct = diffuse + specular + rimLight * RIM_STRENGTH;

    float3 color = shadowCol + SoftClip(direct, LIGHT_KNEE);
    color += rimColor.rgb * fresnel * rimColor.a; // マテリアル指定の固定リム

    // ---- 距離フォグ ----
    // 深度テクスチャを使わず worldPos から出すので、フォワードでもそのまま効く
    const float viewDist = length(cameraPos.xyz - input.worldPos);
    float fog = saturate((viewDist - FOG_START) / max(FOG_END - FOG_START, 1e-3f));

    // 高い位置ほど薄く。床付近に溜まったもやに見せる
    fog *= saturate(1.0f - max(input.worldPos.y - FOG_TOP, 0.0f) / FOG_FALLOFF);
    fog *= FOG_DENSITY;

    // 色は環境光から作る。ライトの色を変えればフォグも自動で追従する
    const float3 fogColor = ambientColor.rgb * FOG_TINT;
    color = lerp(color, fogColor, fog);

    return float4(color, input.col.a * texColor.a);
}
