#ifndef ___LIGHTING_HLSLI___
#define ___LIGHTING_HLSLI___

#define MAX_LIGHTS 10   // ShaderTypes.hpp の MAX_LIGHTS と必ず一致させること

struct LightData
{
    float4 dir;
    float4 color;
    float4 posRange;
    float4 param;
};

cbuffer Light : register(b2)
{
    float4 ambientColor;
    float4 lightCount;
    float4x4 lightviewproj;
    float4 shadowParams;
    LightData lights[MAX_LIGHTS];
}

void ComputeLight(LightData light, float3 worldPos, out float3 l, out float atten)
{
    const int type = (int) light.param.x;
    atten = 1.0f;

    if (type == 0) // Directional
    {
        l = -light.dir.xyz;
        return;
    }

    float3 toLight = light.posRange.xyz - worldPos;
    float dist = length(toLight);
    l = toLight / max(dist, 0.0001f);

    float range = max(light.posRange.w, 0.0001f);
    atten = saturate(1.0f - dist / range);
    atten *= atten;

    if (type == 2) // Spot
    {
        float cosAngle = dot(-l, normalize(light.dir.xyz));
        float spotCos = light.param.y;
        float spotFactor = saturate((cosAngle - spotCos) / max(1.0f - spotCos, 0.0001f));
        atten *= spotFactor;
    }
    if (type == 3)
    {
        float3 axis = normalize(light.dir.xyz);
        l = -axis; // 平行光 → 入射は -dir
        float3 toP = worldPos - light.posRange.xyz;
        float t = dot(toP, axis); // ビームに沿った距離
        float len = max(light.posRange.w, 0.0001f);
        if (t < 0.0f || t > len)
        {
            atten = 0.0f;
            return;
        } // ビーム区間外
        float perp = length(toP - axis * t); // ビーム軸からの垂直距離
        float radius = max(light.param.z, 0.001f);
        float r = saturate(1.0f - perp / radius);
        atten = r * r; // 半径外は0、中心で最大
        return;
    }
}


#endif // ___LIGHTING_HLSLI___