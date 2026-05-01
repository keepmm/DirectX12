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
    float2 uv : TEXCOORD;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 ToonPS(PSInput input) : SV_TARGET
{
    float4 texcolor = g_Texture.Sample(g_Sampler, input.uv);

    float3 n = normalize(input.normal);
    float3 l = normalize(-lightDir.xyz);
    float ndotl = saturate(dot(n, l));

    float toonLevel = (ndotl > 0.7f) ? 1.0f : (ndotl > 0.35f) ? 0.6f : 0.25f;

    float3 baseColor = input.col.rgb * texcolor.rgb;
    float3 diffuse = lightColor.rgb * baseColor * toonLevel;
    float3 ambient = baseColor * ambientColor.rgb;

    return float4(diffuse + ambient, input.col.a * texcolor.a);
}