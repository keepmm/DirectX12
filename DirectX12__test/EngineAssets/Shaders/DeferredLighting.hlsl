#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Albedo : register(t0);
Texture2D g_Emissive : register(t1);
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
    float3 orm = g_ORM.Sample(g_Sampler, input.uv).rgb;
    float metallic = orm.x;
    float roughness = orm.y;
    float ao = orm.z;

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
        // 影響圏外のライトはここで捨てる。届かない灯まで評価すると
        // ランプ参照ぶんの負荷がそのまま灯数倍になり、暗部も灯数ぶん持ち上がる
        if (atten <= 1e-3f) continue;
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
        float3 F0 = lerp(0.04f, baseColor, metallic);
        float ndotv = saturate(dot(N, V));

        // 拡散：最粗ミップ＝コサイン畳み込み済みの放射照度
        float3 irradiance = g_Env.SampleLevel(g_Sampler, DirToEquirect(N), maxMip).rgb;
        float3 kD = (1.0f - F0) * (1.0f - metallic);
        float3 diffuseIBL = irradiance * baseColor * kD * envParam.y;

        // 鏡面：最も粗い2ミップは拡散用なので使わない。第2項は解析近似
        float3 R = reflect(-V, N);
        float3 prefiltered = g_Env.SampleLevel(g_Sampler, DirToEquirect(R), roughness * max(maxMip - 2.0f, 0.0f)).rgb;
        float2 dfg = EnvBRDFApprox(roughness, ndotv);
        float3 specularIBL = prefiltered * (F0 * dfg.x + dfg.y);

        // AO は間接光にだけ掛ける（直接光まで落とすと不自然に潰れる）
        color += (diffuseIBL + specularIBL) * ao;
    }
    else
    {
        color += baseColor * ambientColor.rgb * ao;
    }

    color += g_Emissive.Sample(g_Sampler, input.uv).rgb;

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

// レイと球の交差区間を返す。交差しなければ false
bool RaySphere(float3 ro, float3 rd, float3 c, float r, out float t0, out float t1)
{
    float3 oc = ro - c;
    float b = dot(oc, rd);
    float cc = dot(oc, oc) - r * r;
    float h = b * b - cc;
    t0 = t1 = 0.0f;
    if (h < 0.0f)
        return false;
    h = sqrt(h);
    t0 = -b - h;
    t1 = -b + h;
    return true;
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

    const int STEPS = 8; // 交差区間だけを刻むので8で足りる(下の適応ステップで更に減る)
    const float g = 0.3f; // 前方散乱の鋭さ(横から見ても筋が見えるように0.6→0.3)

    // ディザで開始位置をずらしてバンディングを消す
    float jitter = Hash(input.uv * shadowParams.z);

    float3 scatter = 0;
    const int count = (int) lightCount.x;

    [loop]
    for (int i = 0; i < count; ++i)
    {
        float density = lights[i].param.w;
        int type = (int) lights[i].param.x;
        if (density <= 0.0f || type < 2)
            continue;

        // 影響圏の球。頂点中心・半径 range だと円錐の実体積の10倍近くを
        // 無駄に刻むことになるので、スポットは円錐に、ビームは円柱に外接させる
        float3 axis = normalize(lights[i].dir.xyz);
        float  len = max(lights[i].posRange.w, 0.0001f);
        float3 bCenter;
        float  bRadius;

        if (type == 3) // ビーム: 半径 param.z の円柱
        {
            float r = max(lights[i].param.z, 0.001f);
            bCenter = lights[i].posRange.xyz + axis * (len * 0.5f);
            bRadius = sqrt(len * len * 0.25f + r * r);
        }
        else // スポット: 半角 acos(param.y) の円錐
        {
            float cosT = clamp(lights[i].param.y, 0.09f, 0.9999f);
            float cos2 = cosT * cosT;
            if (cos2 > 0.5f)
            {
                // 半角45°以下。頂点と底面リムを通る外接球(中心は軸上 len/(2cos^2))
                float c = len / (2.0f * cos2);
                bCenter = lights[i].posRange.xyz + axis * c;
                bRadius = c;
            }
            else
            {
                // 半角45°超。底面の円を包む球のほうが小さい
                float r = len * sqrt(1.0f - cos2) / cosT;
                bCenter = lights[i].posRange.xyz + axis * len;
                bRadius = r;
            }
        }

        // このピクセルのレイが影響圏を通らなければ即スキップ
        float t0, t1;
        if (!RaySphere(camPos, rayDir, bCenter, bRadius, t0, t1))
            continue;

        t0 = max(t0, 0.0f);
        t1 = min(t1, rayLen);
        if (t1 <= t0)
            continue;

        // 交差区間だけを刻む(p はライトごとに必ず初期化する)
        float segLen = t1 - t0;
        // 区間が短いライト(遠い/かすめただけ)はステップ数も減らす。
        // ステージのように灯が数十本あると、この差が丸ごと効いてくる
        int steps = clamp((int) (segLen * 0.5f), 3, STEPS);
        float stepLen = segLen / steps;
        float3 p = camPos + rayDir * (t0 + stepLen * jitter);

        float3 lightScatter = 0;

        [loop]
        for (int s = 0; s < steps; ++s)
        {
            float3 L;
            float atten;
            ComputeLight(lights[i], p, L, atten);
            if (atten > 0.0f)
            {
                float sh = (i == 0) ? ShadowFactor1(p) : 1.0f;
                float phase = PhaseHG(dot(-rayDir, L), g);
                lightScatter += atten * phase * sh;
            }
            p += rayDir * stepLen;
        }

        // stepLen がライトごとに異なるのでここで積分係数を掛ける
        scatter += lights[i].color.rgb * lightScatter * density * (0.5f * stepLen);
    }

    return float4(scatter, 1.0f);
}
