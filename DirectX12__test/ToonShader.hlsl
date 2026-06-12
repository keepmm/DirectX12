#define MAX_LIGHTS 10

cbuffer Frame : register(b0)
{
    float4x4 viewProj;
}

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
    LightData lights[MAX_LIGHTS];
}

struct PSInput
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float3 worldPos : TEXCOORD1;
    float4 col : COLOR;
    float2 uv : TEXCOORD;
};

Texture2D g_Texture : register(t0);
SamplerState g_Sampler : register(s0);

void ComputeLight(LightData light, float3 worldPos, out float3 l, out float atten)
{
    // ライトタイプの取得
    const int type = (int) light.param.x;
    atten = 1.0f;
    
    // Directional Light
    if (type == 0)
    {
        l = (-light.dir.xyz);
        return;
    }
    
    // Point / Spot ライト
    float3 toLight = light.posRange.xyz - worldPos;
    float dist = length(toLight);
    l = toLight / max(dist, 0.0001f);
    
    float range = max(light.posRange.w, 0.0001f);
    atten = saturate(1.0f - dist / range);
    atten *= atten; // 二乗で減衰
    
    if (type == 2)
    {
        float cosAngle = dot(-l, normalize(light.dir.xyz));
        float spotCos = light.param.y;
        float spotFactor = saturate((cosAngle - spotCos) / max(1.0f - spotCos, 0.0001f));
        atten *= spotFactor;
    }
}

float4 ToonPS(PSInput input) : SV_TARGET
{
    float4 texcolor = g_Texture.Sample(g_Sampler, input.uv);

    float3 n = normalize(input.normal);
    float3 baseColor = input.col.rgb * texcolor.rgb;

    float3 diffuse = float3(0.0f, 0.0f, 0.0f);
    const int count = (int) lightCount.x;
    for (int i = 0; i < count; ++i)
    {
        float3 l;
        float atten;
        ComputeLight(lights[i], input.worldPos, l, atten);

        float ndotl = saturate(dot(n, l)) * atten;
        float toonLevel = (ndotl > 0.7f) ? 1.0f : (ndotl > 0.35f) ? 0.6f : 0.25f;

        diffuse += lights[i].color.rgb * baseColor * toonLevel;
    }

    float3 ambient = baseColor * ambientColor.rgb;
    return float4(diffuse + ambient, input.col.a * texcolor.a);
}