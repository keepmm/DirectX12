cbuffer TransformBuffer : register(b0)
{
    float4x4 viewProj;
}

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VSOutput LineVS(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), viewProj);
    output.color = input.color;
    return output;
}

float4 LinePS(VSOutput input) : SV_TARGET
{
    return input.color;
}