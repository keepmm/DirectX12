#include "Common.hlsli"
#include "Lighting.hlsli"

cbuffer BoneMatrices : register(b4)
{
    float4x4 bones[512];
}

float4 ShadowVS(VSInput pin) : SV_Position
{
    float4 pos = float4(pin.pos, 1.0f);

    // スキンメッシュはボーンで動かしてから影を落とす。
    // これをしないとキャラの影だけバインドポーズのまま残る
    float wsum = pin.boneWeights.x + pin.boneWeights.y
               + pin.boneWeights.z + pin.boneWeights.w;
    if (wsum > 0.001f)
    {
        float4 w = pin.boneWeights / wsum;
        float4x4 m =
            bones[pin.boneIndices.x] * w.x +
            bones[pin.boneIndices.y] * w.y +
            bones[pin.boneIndices.z] * w.z +
            bones[pin.boneIndices.w] * w.w;
        pos = mul(pos, m);
    }

    float4 wp = mul(pos, world);
    return mul(wp, lightviewproj);
}