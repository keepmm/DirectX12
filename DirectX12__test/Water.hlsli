// ------------------------------------------------------------------ //
//  水面の波の定義。WaterShader.hlsl の VS と PS が同じものを使う。
//  ここを1箇所にしないと、頂点の変位と法線がズレて波頭が破綻する。
//
//  前提: MaterialCB.hlsli を先に include してあること(waveParams を読む)
// ------------------------------------------------------------------ //
#ifndef ___WATER_HLSLI___
#define ___WATER_HLSLI___

static const float WATER_PI2 = 6.283185307179586f;
static const float WATER_PI = 3.141592653589793f;
static const float WATER_G = 9.8f; // 重力加速度

// 大きなうねり。向きをばらすほど「うねり」に見える
static const float2 kSwellDir[4] =
{
    float2(1.00f, 0.00f),
    float2(0.31f, 0.95f),
    float2(-0.71f, 0.71f),
    float2(0.60f, -0.80f),
};
static const float kSwellK[4] = { 1.0f, 2.3f, 4.1f, 8.7f }; // 波長の逆比
static const float kSwellA[4] = { 1.0f, 0.5f, 0.25f, 0.12f }; // 振幅の比

// 細かいさざ波。うねりより十分短く、かつ整数比を避けて繰り返しを隠す
static const float2 kRippleDir[3] =
{
    float2(0.86f, 0.51f),
    float2(-0.42f, 0.91f),
    float2(0.98f, -0.19f),
};
static const float kRippleK[3] = { 13.0f, 21.0f, 34.0f };
static const float kRippleA[3] = { 1.0f, 0.6f, 0.35f };

// ------------------------------------------------------------------ //
//  Gerstner 波。頂点を「上下」だけでなく「波頭方向へ寄せる」ので山が尖る。
//
//    位相 ph = k*dot(D,p0) + w*t        k = 2π/波長, w = sqrt(g*k)
//    水平 P.xz = p0 + Σ Q*A*D*cos(ph)   ← これが尖りを作る
//    垂直 P.y  =      Σ   A  *sin(ph)
//
//  法線・接線は上の式をそのまま p0 で微分したもの(解析微分)。
//  Q(尖り)を上げすぎると隣の頂点を追い越して面が裏返る。
//  Q = steepness / (k*A*波の本数) に正規化して、その限界を超えないようにする
// ------------------------------------------------------------------ //
void GerstnerSwell(float2 p0, float t,
                   out float3 offset, out float3 N, out float3 T)
{
    const float baseK = WATER_PI2 / max(waveParams.y, 0.01f); // 一番長い波
    const float baseA = waveParams2.x; // 振幅(m)
    const float speed = waveParams.z;
    const float steep = saturate(waveParams2.y);

    offset = 0.0f;
    // 法線・接線は「平らな面」からの差分を積む。最後に定数項を足す
    float3 dN = 0.0f;
    float3 dT = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const float k = baseK * kSwellK[i];
        const float A = baseA * kSwellA[i];
        if (A <= 1e-6f)
            continue;

        // 深水波の分散関係。短い波ほど速く走るので、重ねると自然に崩れる
        const float w = sqrt(WATER_G * k) * speed;
        const float2 D = kSwellDir[i];

        // 面が裏返らない範囲に尖りを抑える
        const float Q = steep / max(k * A * 4.0f, 1e-6f);

        const float ph = k * dot(D, p0) + w * t;
        const float S = sin(ph);
        const float C = cos(ph);
        const float KA = k * A;

        offset.xz += Q * A * D * C;
        offset.y += A * S;

        dN.xz += D * (KA * C);
        dN.y += Q * KA * S;

        // x 方向の接線。法線マップを使うとき用に一応揃えておく
        dT.x += Q * KA * D.x * D.x * S;
        dT.y += D.x * KA * C;
        dT.z += Q * KA * D.x * D.y * S;
    }

    N = normalize(float3(-dN.x, 1.0f - dN.y, -dN.z));
    T = normalize(float3(1.0f - dT.x, dT.y, -dT.z));
}

// ------------------------------------------------------------------ //
//  細かいさざ波の法線。高さ h = A*sin(dot(D,p)*k + t*w) を重ね、
//  勾配 dh/dp を解析的に出す。法線 = normalize(-dh/dx, 1, -dh/dz)
//  (法線マップより軽く、タイリングの継ぎ目も出ない)
// ------------------------------------------------------------------ //
float3 RippleNormal(float2 p, float t)
{
    const float baseK = WATER_PI2 / max(waveParams.y, 0.01f);
    const float speed = waveParams.z;

    float2 slope = 0.0f;
    [unroll]
    for (int i = 0; i < 3; ++i)
    {
        const float k = baseK * kRippleK[i];
        const float w = sqrt(WATER_G * k) * speed;
        const float ph = dot(kRippleDir[i], p) * k + t * w;
        slope += kRippleDir[i] * (kRippleA[i] * cos(ph));
    }

    slope *= waveParams.x; // 細波の起伏
    return normalize(float3(-slope.x, 1.0f, -slope.y));
}

#endif // ___WATER_HLSLI___