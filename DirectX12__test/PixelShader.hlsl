cbuffer Transform : register(b0)
{
    float4x4 worldViewProj;
    float4x4 world;
    float4 lightDir;
    float4 lightColor;
    float4 ambientColor;
}

    struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float4 col : COLOR;
};

float4 BasicPS(PSInput input) : SV_TARGET
{
    float3 n = normalize(input.normal);
    float3 l = normalize(-lightDir.xyz);
    float ndotl = saturate(dot(n, l));

    float3 diffuse = lightColor.rgb * input.col.rgb * ndotl;
    float3 ambient = input.col.rgb * ambientColor.rgb;

    return float4(diffuse + ambient, input.col.a);
}

float4 WireFramePS(PSInput input) : SV_Target
{
    return float4(0.0f, 0.0f, 0.0f, 1.0f); // ワイヤーフレームは常に黒
}