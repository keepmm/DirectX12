#include "Common.hlsli"
#include "Lighting.hlsli"
#include "BRDF.hlsli"

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

#include "MaterialCB.hlsli"

float4 PhongPS(PSInput pin) : SV_Target
{
    // 通常のLambertライティング
    float4 tex = g_Texture.Sample(g_Sampler, pin.uv);
    float3 baseColor = pin.col.rgb * tex.rgb * basecolor.rgb;
    float3 N = normalize(pin.normal);
    float3 V = normalize(cameraPos.xyz - pin.worldPos);
    
    float shininess = lerp(128.0f, 4.0f, saturate(roughness));
    
    float3 diffuse = 0;
    float3 specular = 0;
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 L;
        float atten;
        ComputeLight(lights[i], pin.worldPos, L, atten);
        // 影響圏外のライトはここで捨てる。届かない灯まで評価すると
        // ランプ参照ぶんの負荷がそのまま灯数倍になり、暗部も灯数ぶん持ち上がる
        if (atten <= 1e-3f) continue;

        diffuse += Lambert(baseColor, lights[i].color.rgb, N, L) * atten;
        specular += BlinnPhongSpec(N, L, V, lights[i].color.rgb, shininess) * atten;
    }

    float3 color = diffuse
                 + baseColor * ambientColor.rgb
                 + specular * metallic; // 鏡面の強さ

    return float4(color, pin.col.a * tex.a * basecolor.a);
}