#include "Common.hlsli"

cbuffer BoneMatrices : register(b4)
{
    float4x4 bones[256];
}

cbuffer Material : register(b3)
{
    float roughness;
    float metallic;
    float2 _pad;
    float4 rimColor;
    float4 mapFlags;
    float4 faceParam; // w: アウトラインの太さ
}

struct OutlineOut
{
    float4 pos : SV_POSITION;
};

OutlineOut Genshin_OutlineVS(VSInput input)
{
    OutlineOut output;

    float wsum = input.boneWeights.x + input.boneWeights.y
               + input.boneWeights.z + input.boneWeights.w;

    float4 pos = float4(input.pos, 1.0f);
    float3 normal = input.normal;

    if (wsum > 0.001f)
    {
        float4x4 m =
                bones[input.boneIndices.x] * input.boneWeights.x +
                bones[input.boneIndices.y] * input.boneWeights.y +
                bones[input.boneIndices.z] * input.boneWeights.z +
                bones[input.boneIndices.w] * input.boneWeights.w;
        pos = mul(pos, m);
        normal = mul(float4(input.normal, 0.0f), m).xyz;
    }

    float4 wp = mul(pos, world);
    float3 wn = normalize(mul(float4(normal, 0.0f), world).xyz);

    // カメラ距離で太さを一定に保つ
    float dist = length(cameraPos.xyz - wp.xyz);
    float width = max(faceParam.w, 0.001f) * dist * 0.015f;
    wp.xyz += wn * width;

    output.pos = mul(wp, viewProj);
    return output;
}

float4 Genshin_OutlinePS() : SV_TARGET
{
    return float4(0.05f, 0.05f, 0.07f, 1.0f); // 完全な黒ではなく少し青みがかった濃紺
}