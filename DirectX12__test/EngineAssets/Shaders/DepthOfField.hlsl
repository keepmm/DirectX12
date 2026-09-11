// 被写界深度。深度から錯乱円(CoC)を出し、ボケ側だけを半解像度で丸くぼかして合成する。
//
// 各パスの出力先は「ウィンドウ全体ぶんのRT」で、実際に使うのは左上のサブ矩形だけ。
// ビューポートのサイズはシーンRT(パネルサイズ)に合わせるので、
// サンプルするときは uvScale = シーンRT / ウィンドウ を掛けて位置を合わせる。

Texture2D g_Sharp : register(t0);   // 合成前のシーン(等倍)
Texture2D g_Blur : register(t1);    // ぼかし済み(半分)
Texture2D g_Depth : register(t2);   // シーン深度
SamplerState g_Sampler : register(s0);

cbuffer Dof : register(b0)
{
    float4 focus;   // x:焦点距離 y:合焦する幅 z:最大ボケ半径(px) w:ボケの立ち上がり距離
    float4 proj;    // x:nearZ y:farZ zw:深度テクスチャのuvScale
    float4 uv;      // xy:uvScale zw:出力のテクセルサイズ
}

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

// 深度バッファの値をカメラからの距離へ戻す(逆Zではない通常の射影)
float LinearDepth(float d)
{
    const float n = proj.x;
    const float f = proj.y;
    return (n * f) / max(f - d * (f - n), 1e-6f);
}

// 錯乱円。0で合焦、1で最大ボケ
float CoC(float2 texUv)
{
    // カラー(uv.xy)と深度(proj.zw)でサブ矩形の大きさが違う。
    // 本描画はHDRシーン=ウィンドウ全面へ行うので深度は全面、
    // カラーはシーンRTぶんしか埋まっていない。ここを共用すると
    // 深度を拡大して読むことになり、ピントの位置がまるでずれる
    const float z = LinearDepth(g_Depth.SampleLevel(g_Sampler, texUv * proj.zw, 0).r);

    // 焦点からの距離。合焦幅のぶんだけは完全にシャープに保つ
    float d = abs(z - focus.x) - focus.y;
    return saturate(max(d, 0.0f) / max(focus.w, 1e-4f));
}

// ---- 半解像度への縮小。同時にCoCをαへ入れる ----
float4 DofDownsamplePS(VSOut input) : SV_TARGET
{
    const float3 c = g_Sharp.SampleLevel(g_Sampler, input.uv * uv.xy, 0).rgb;
    return float4(c, CoC(input.uv));
}

// ---- 円形ボケ ----
// 黄金角で並べた16タップ。分離ガウスだと四角く伸びるので、
// 玉ボケを丸くしたいここでは円板状に散らす
#define DOF_TAPS 16

float4 DofBlurPS(VSOut input) : SV_TARGET
{
    const float4 center = g_Blur.SampleLevel(g_Sampler, input.uv, 0);
    const float radius = center.a * focus.z;

    if (radius < 0.5f)
    {
        return center;   // 合焦しているところは触らない
    }

    float3 sum = center.rgb;
    float weight = 1.0f;

    // 2.39996 rad = 黄金角。回しながら半径を広げると均等に散る
    [unroll]
    for (int i = 0; i < DOF_TAPS; ++i)
    {
        const float t = (float) (i + 1) / DOF_TAPS;
        const float a = 2.39996f * (i + 1);
        const float2 offset = float2(cos(a), sin(a)) * sqrt(t) * radius * uv.zw;

        const float4 s = g_Blur.SampleLevel(g_Sampler, input.uv + offset, 0);

        // 手前の合焦物が背景へにじまないよう、ボケている画素ほど強く採る
        const float w = s.a;
        sum += s.rgb * w;
        weight += w;
    }

    return float4(sum / weight, center.a);
}

// ---- 合成。CoCでシャープとボケを混ぜる ----
float4 DofCompositePS(VSOut input) : SV_TARGET
{
    const float3 sharp = g_Sharp.SampleLevel(g_Sampler, input.uv * uv.xy, 0).rgb;
    const float3 blur = g_Blur.SampleLevel(g_Sampler, input.uv * uv.xy, 0).rgb;

    // 合焦から外れるほどボケ側へ。CoCを少し寝かせて境界を目立たなくする
    const float k = smoothstep(0.0f, 1.0f, CoC(input.uv));
    return float4(lerp(sharp, blur, k), 1.0f);
}
