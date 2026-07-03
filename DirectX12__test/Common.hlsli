#ifndef ___COMMON_HLSLI___
#define ___COMMON_HLSLI___

cbuffer Frame : register(b0)
{
    float4x4 viewProj;
    float4 cameraPos;
}
cbuffer Object : register(b1)
{
    float4x4 world;
}

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
    float3 tangent : TANGENT;
};

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float3 worldPos : TEXCOORD1;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
    float3 tangent : TANGENT;
};

#endif // ___COMMON_HLSLI___