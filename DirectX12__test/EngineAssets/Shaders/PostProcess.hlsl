Texture2D g_Src : register(t0);
Texture2D g_Bloom : register(t1);
SamplerState g_Sampler : register(s0);

cbuffer PostCB : register(b0)
{
    float2 texelSize;
    float threshold;
    float intensity;
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

float4 BrightPS(VSOut i) : SV_TARGET
{
    float3 c = g_Src.Sample(g_Sampler, i.uv).rgb;
    float lum = max(c.r, max(c.g, c.b));
    float k = saturate((lum - threshold) / max(lum, 1e-4));
    return float4(c * k, 1.0f);
}

// ガウシアンぶらー
float4 BlurPS(VSOut i) : SV_TARGET
{
    const float w[5] = { 0.227027, 0.194594, 0.121621, 0.054054, 0.016216 };
    float3 c = g_Src.Sample(g_Sampler, i.uv).rgb * w[0];
    [unroll]
    for (int k = 1; k < 5; ++k)
    {
        c += g_Src.Sample(g_Sampler, i.uv + texelSize * k).rgb * w[k];
        c += g_Src.Sample(g_Sampler, i.uv - texelSize * k).rgb * w[k];
    }
    return float4(c, 1.0f);
}

float3 ACESFilm(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f,d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 CompositePS(VSOut i) : SV_TARGET
{
    float3 scene = g_Src.Sample(g_Sampler, i.uv).rgb;
    float3 bloom = g_Bloom.Sample(g_Sampler, i.uv).rgb;
    float3 c = scene + bloom * intensity;
    
    c *= 1.2f;
    c = ACESFilm(c);
//    c = pow(c, 1.9f / 2.2f);
    return float4(c, 1.0f);
}

float4 CopyPS(VSOut i) : SV_TARGET
{
    return float4(g_Src.Sample(g_Sampler, i.uv).rgb, 1.0f);
}