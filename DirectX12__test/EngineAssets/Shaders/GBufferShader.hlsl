#include "Common.hlsli"

Texture2D g_Texture : register(t0);
Texture2D g_Normal  : register(t2);
Texture2D g_Metal   : register(t3);
Texture2D g_Rough :   register(t4);
Texture2D g_Emissive  : register(t7);
Texture2D g_Occlusion : register(t9);
SamplerState g_Sampler : register(s0);

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags; // x: hasNormal, y: hasMetal, z: hasRough, w: envMaxMip
    float4 faceParam;
    float4 sssParams;
    float4 sssColor;
    float4 basecolor;
    float4 reflectParam;
    float4 pbrParams;     // x: hasEmissive, y: hasOcclusion, z: エミッシブ強度
    float4 emissiveColor;
}

struct GbufferOutput
{
    float4 albedo   : SV_TARGET0; // rgb : albedo 
    float4 normal   : SV_TARGET1; // rgb : encode(N)
    float4 orm      : SV_TARGET2; // r : metallic, g : roughness, b : occlusion
    float4 emissive : SV_TARGET3; // rgb : emissive
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
    float r = (mapFlags.z > 0.5f) ? g_Rough.Sample(g_Sampler, input.uv).r : roughness;
    
    float ao = (pbrParams.y > 0.5f) ? g_Occlusion.Sample(g_Sampler, input.uv).r : 1.0f;

    float3 emissive = emissiveColor.rgb * pbrParams.z;
    if (pbrParams.x > 0.5f)
        emissive *= g_Emissive.Sample(g_Sampler, input.uv).rgb;

    o.albedo   = float4(albedo, input.col.a);
    o.normal   = float4(N * 0.5f + 0.5f, 0.0f);
    o.orm      = float4(m, r, ao, 0.0f);
    o.emissive = float4(emissive, 1.0f);
    return o;
}