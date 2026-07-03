// ShaderTemplates.hpp
inline const char* kPixelShaderTemplate = R"(#define MAX_LIGHTS 10
cbuffer Frame : register(b0) { float4x4 viewProj; }
struct LightData { float4 dir; float4 color; float4 posRange; float4 param; };
cbuffer Light : register(b2) { float4 ambientColor; float4 lightCount; LightData lights[MAX_LIGHTS]; }
struct PSInput {
    float4 pos : SV_POSITION; float3 normal : NORMAL;
    float3 worldPos : TEXCOORD1; float4 col : COLOR; float2 uv : TEXCOORD;
};
Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

float4 main(PSInput input) : SV_Target
{
    float4 tex = g_Texture.Sample(g_Sampler, input.uv);
    return float4(input.col.rgb * tex.rgb, input.col.a * tex.a);
}
)";