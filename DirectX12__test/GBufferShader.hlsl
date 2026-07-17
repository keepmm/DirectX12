#include "Common.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_Normal  : register(t2);
Texture2D g_Metal   : register(t3);
Texture2D g_Rough :   register(t4);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags; // x: useAlbedoMap, y: useNormalMap, z: useMetallicMap, w: useRoughnessMap
}

struct GbufferOutput
{
    float4 albedo : SV_TARGET0; // rgb : albedo 
    float4 normal : SV_TARGET1; // rgb : encode(N)
    float4 orm    : SV_TARGET2; // r : metallic, g : roughness;
};

GbufferOutput GBufferPS(PSInput input)
{
    GbufferOutput o;
    
    float3 albedo = input.col.rgb * g_Texture.Sample(g_Sampler, input.uv).rgb;
    
    float3 N = normalize(input.normal);
    if(mapFlags.x > 0.5f)
    {
        float3 T = normalize(input.tangent);
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T);
        float3 nTex = g_Normal.Sample(g_Sampler, input.uv).rgb * 2.0f - 1.0f;
        N = normalize(mul(nTex, float3x3(T, B, N)));
    }
    
    float m = (mapFlags.y > 0.5f) ? g_Metal.Sample(g_Sampler, input.uv).r : metallic;
    float r = (mapFlags.x > 0.5f) ? g_Rough.Sample(g_Sampler, input.uv).r : roughness;
    
    o.albedo = float4(albedo, input.col.a);
    o.normal = float4(N * 0.5f + 0.5f, 0.0f);
    o.orm    = float4(m, r, 0.0f, 0.0f);
    return o;
}