#include "Common.hlsli"

Texture2D gPanorma : register(t0);
SamplerState gSampler : register(s0);

struct SkyVSOut
{
    float4 pos : SV_POSITION;
    float3 dir : TEXCOORD;
};

SkyVSOut SkyboxVS(VSInput pin)
{
    SkyVSOut o;
    o.dir = pin.pos;
    
    // カメラ位置に平行移動
    float4 p = mul(float4(pin.pos + cameraPos.xyz, 1.0f), viewProj);
    
    
    // z = wにして深度を必ず1.0にする
    o.pos = p.xyww;
    
    return o;
}

static const float PI = 3.14159265359f;

float4 SkyboxPS(SkyVSOut pin) : SV_Target
{
    float3 d = normalize(pin.dir);
    
    // 方向ベクトル
    float u = atan2(d.z, d.x) / (2.0f * PI) + 0.5f;
    float v = acos(clamp(d.y, -1.0f, 1.0f)) / PI;
    
    float3 hdr = gPanorma.Sample(gSampler, float2(u, v)).rgb;
    
    // 簡易トーンマップ
    float3 mapped = hdr / (hdr + 1.0f);
    mapped = pow(mapped, 1.0f / 2.2f);
  
    return float4(mapped, 1.0f);
}