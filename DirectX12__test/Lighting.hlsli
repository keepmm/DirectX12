#ifndef ___LIGHTING_HLSLI___
#define ___LIGHTING_HLSLI___

#define MAX_LIGHTS 100

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
}


#endif // ___LIGHTING_HLSLI___