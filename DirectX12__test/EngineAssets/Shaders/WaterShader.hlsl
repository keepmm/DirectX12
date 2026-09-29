/*****************************************************************//**
 * \file   WaterShader.hlsl
 * \brief  水面。フレネルで「浅い角度＝鏡、真上＝透ける」を作る。
 *         波は sin の重ね合わせを解析微分して法線にする(テクスチャ不要)
 *
 * 作成者 keeep
 * 作成日 2026/9/19
 * 更新履歴 9.19 新規作成
 *********************************************************************/
#include "Common.hlsli"
#include "Lighting.hlsli"

Texture2D g_Texture    : register(t0);
Texture2D g_Env        : register(t5);
Texture2D g_Reflection : register(t8);
SamplerState g_Sampler : register(s0);

#include "MaterialCB.hlsli"
#include "Water.hlsli"        // MaterialCB の後に include すること(waveParams を読む)

// SkyBoxShader.hlsl と同じ向きに揃えること
float2 WaterDirToEquirect(float3 d)
{
    return float2(atan2(d.z, d.x) / WATER_PI2 + 0.5f,
                  acos(clamp(d.y, -1.0f, 1.0f)) / WATER_PI);
}

// ------------------------------------------------------------------ //
//  頂点シェーダー。うねりで頂点を動かし、その場の正確な法線を PS へ渡す。
//  PS 側は input.worldPos が「変位後」なので、大波をもう一度評価できない。
//  だから大波の法線はここで確定させる
// ------------------------------------------------------------------ //
PSInput WaterVS(VSInput input)
{
    PSInput output;

    // 波はワールドXZで評価する。ローカル座標だと water を動かすと波もついてくる
    const float4 wp0 = mul(float4(input.pos, 1.0f), world);
    const float t = cameraPos.w;

    float3 offset, N, T;
    GerstnerSwell(wp0.xz, t, offset, N, T);

    const float3 wp = wp0.xyz + offset;

    output.pos = mul(float4(wp, 1.0f), viewProj);
    output.worldPos = wp;
    // 板が傾いていてもいいように、法線・接線は world で回してから渡す
    output.normal = normalize(mul(float4(N, 0.0f), world).xyz);
    output.tangent = normalize(mul(float4(T, 0.0f), world).xyz);
    output.col = input.col;
    output.uv = input.uv;
    return output;
}

float4 WaterPS(PSInput input) : SV_TARGET
{
    const float t = cameraPos.w;

    // うねりの法線は VS が計算済み。ここでは細波を上乗せするだけ
    const float3 Nswell = normalize(input.normal);
    const float3 Nrip = RippleNormal(input.worldPos.xz, t);
    // 平らな面(0,1,0)からの差分として足す = うねりの向きを崩さず細波を乗せる
    float3 N = normalize(Nswell + (Nrip - float3(0.0f, 1.0f, 0.0f)));

    float3 V = normalize(cameraPos.xyz - input.worldPos);
    if (dot(N, V) < 0.0f) N = -N;          // 水中から見上げた場合
    const float ndotv = saturate(dot(N, V));

    // ---- フレネル(Schlick) ----
    // 水の垂直反射率は 2%。真上から見ればほぼ透け、浅い角度で鏡になる。
    // この1行が「水らしさ」の8割を作っている
    const float F = 0.02f + 0.98f * pow(1.0f - ndotv, 5.0f);

    // ---- 反射色 ----
    // 平面反射RTがあればそれを優先。無ければ環境マップ(空)で代用
    float3 refl;
    if (reflectParam.x > 0.0f)
    {
        float2 rsize;
        g_Reflection.GetDimensions(rsize.x, rsize.y);
        // 反射RTはシーンRTの reflectParam.w 倍(ToonShader と同じ規約)
        float2 ruv = input.pos.xy * reflectParam.w / rsize;
        // 波の傾きでUVをずらす = 映り込みが揺れる。画面空間なので横ずれで足る
        ruv += (N.xz - Nswell.xz) * 0.08f;
        refl = g_Reflection.Sample(g_Sampler, saturate(ruv)).rgb * reflectParam.x;
    }
    else if (mapFlags.w > 0.0f)
    {
        const float3 R = reflect(-V, N);
        // 最も粗い2ミップは拡散用に潰してあるので使わない(GlassShader と同じ)
        refl = g_Env.SampleLevel(g_Sampler, WaterDirToEquirect(R),
                                 roughness * max(mapFlags.w - 2.0f, 0.0f)).rgb;
    }
    else
    {
        refl = ambientColor.rgb;
    }

    // ---- 透過側の色 ----
    // シーンカラーを読めないので、ベースカラーを「水を通して見える色」として扱う。
    // 濁りが強いほど暗く沈ませる(Beer-Lambert の定数近似)
    const float3 tint = basecolor.rgb * g_Texture.Sample(g_Sampler, input.uv).rgb;
    const float3 body = lerp(tint, tint * 0.5f, saturate(waterParams.y) * 0.5f);

    // ---- 太陽のきらめき ----
    float3 spec = 0.0f;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], input.worldPos, L, atten);
        if (atten <= 1e-3f) continue;

        const float3 H = normalize(L + V);
        // 水は非常に滑らか。指数を大きく取ると点状のきらめきになる
        const float power = exp2(lerp(12.0f, 6.0f, saturate(roughness)))
                          * max(waveParams.w, 0.01f);
        spec += lights[i].color.rgb * pow(saturate(dot(N, H)), power) * atten;
    }

    // ---- 白波(波の頂点だけ白くする) ----
    float3 foam = 0.0f;
    if (waterParams.z > 0.0f)
    {
        // 傾きの大きい = 崩れかけている所を拾う
        const float crest = saturate((1.0f - N.y) / max(waterParams.z, 0.001f));
        foam = crest * crest * 0.6f;
    }

    float3 color = lerp(body, refl, F) + spec + foam;

    // 縁ほど不透明(反射が強くなるぶん向こうが見えなくなる)
    const float alpha = saturate(max(waterParams.x, F) * faceParam.y);

    return float4(color, alpha);
}
