#include "Common.hlsli"

cbuffer BoneMatrices : register(b4)
{
    float4x4 bones[512];
    float3 pad;
    float morphActive; // モーフが有効かどうか 0.0 = 無効, 1.0 = 有効
}

StructuredBuffer<float3> g_MorphOffsets : register(t7); // 頂点index対応のブレンド済みオフセット

PSInput SkinnedVS(VSInput input, uint vertexId : SV_VertexID)
{
    PSInput output;

    float wsum = input.boneWeights.x + input.boneWeights.y
               + input.boneWeights.z + input.boneWeights.w;

    // モーフを先に適用してからスキニング
    float3 morph = float3(0, 0, 0);
    [branch]
    if (morphActive > 0.5)
    {
        morph = g_MorphOffsets[vertexId];
    }
    float4 pos = float4(input.pos + morph, 1.0f);
    float4 skinned;
    float3 skinnedNormal;

    if (wsum > 0.001f)
    {
        // PMX の BDEF4 / QDEF はウェイトの合計が 1 である保証がない。
        // 正規化せずに合成すると行列が wsum 倍され、頂点が原点方向へ
        // 伸縮して指や手首が尖った形に破綻する。
        float4 w = input.boneWeights / wsum;

        float4x4 m =
                bones[input.boneIndices.x] * w.x +
                bones[input.boneIndices.y] * w.y +
                bones[input.boneIndices.z] * w.z +
                bones[input.boneIndices.w] * w.w;

        skinned = mul(pos, m);
        skinnedNormal = mul(float4(input.normal, 0.0f), m).xyz;
    }
    else
    {
        skinned = pos;
        skinnedNormal = input.normal;
    }

    float4 wp = mul(skinned, world);
    output.pos = mul(wp, viewProj);
    output.worldPos = wp.xyz;
    output.normal = normalize(mul(float4(skinnedNormal, 0.0f), world).xyz);
    output.col = input.col;
    output.uv = input.uv;
    output.tangent = normalize(mul(float4(input.tangent, 0.0f), world).xyz);
    return output;
}