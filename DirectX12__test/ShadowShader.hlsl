#include "Common.hlsli"
#include "Lighting.hlsli"

float4 ShadowVS(VSInput pin) : SV_Position
{
    float4 wp = mul(float4(pin.pos, 1.0f), world);
    return mul(wp, lightviewproj);
}