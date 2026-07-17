#ifndef BRDF_HLSLI
#define BRDF_HLSLI

// --- Lambert（今の BasicPS 相当）---
float3 Lambert(float3 baseColor, float3 lightColor, float3 N, float3 L)
{
    return lightColor * baseColor * saturate(dot(N, L));
}

// --- Rim（フレネル風の縁光り）---
// rimColor / rimPower はマテリアルから渡す想定
float3 Rim(float3 N, float3 V, float3 rimColor, float rimPower)
{
    float rim = 1.0f - saturate(dot(N, V));
    return rimColor * pow(rim, rimPower);
}

// --- PBR: Cook-Torrance の各項 ---
static const float PI = 3.14159265f;

float D_GGX(float ndoth, float rough)
{
    float a = rough * rough;
    float a2 = a * a;
    float d = (ndoth * ndoth) * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 1e-5f);
}
float G_Smith(float ndotv, float ndotl, float rough)
{
    float k = (rough + 1.0f);
    k = k * k / 8.0f;
    float gv = ndotv / (ndotv * (1.0f - k) + k);
    float gl = ndotl / (ndotl * (1.0f - k) + k);
    return gv * gl;
}
float3 F_Schlick(float vdoth, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - vdoth, 5.0f);
}

float Fresnel(float3 N,float3 V,float power)
{
    return pow(1.0f - saturate(dot(N, V)), power);
}

// 1ライト分の Cook-Torrance
float3 CookTorrance(float3 albedo, float metallic, float rough,
                    float3 N, float3 V, float3 L, float3 lightColor)
{
    float3 H = normalize(V + L);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V)) + 1e-5f;
    float ndoth = saturate(dot(N, H));
    float vdoth = saturate(dot(V, H));

    float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float D = D_GGX(ndoth, rough);
    float G = G_Smith(ndotv, ndotl, rough);
    float3 F = F_Schlick(vdoth, f0);

    float3 spec = (D * G) * F / (4.0f * ndotv * ndotl + 1e-5f);
    float3 kd = (1.0f - F) * (1.0f - metallic);
    float3 diff = kd * albedo / PI;
    return (diff + spec) * lightColor * ndotl;
}

// --- Blinn-Phong スペキュラ ---
float3 BlinnPhongSpec(float3 N, float3 L, float3 V, float3 lightColor, float shininess)
{
    float3 H = normalize(L + V);
    float s = pow(saturate(dot(N, H)), shininess);
    return lightColor * s;
}

// ---- Cloth Charlie分布 ---- //
float D_Charlie(float ndoth,float rough)
{
    float invR = 1.0f / max(rough, 1e-3f);
    float sin2h = max(1.0f - ndoth * ndoth, 1e-4f);
    return (2.0f + invR) * pow(sin2h, invR * 0.5f) / (2.0f * PI);
}

float V_Ashikhman(float ndotl,float ndotv)
{
    return 1.0f / (4.0f * (ndotl + ndotv - ndotl * ndotv) + 1e-4f);
}

float3 ClothBRDF(float3 albedo,float3 sheenColor,float rougth,
float3 N,float3 V,float3 L,float3 lightColor)
{
    float3 H = normalize(V + L);
    float ndotl = saturate(dot(N, L));
    float ndotv = saturate(dot(N, V)) + 1e-5f;
    float ndoth = saturate(dot(N, H));
    
    // 繊維のシーンローブ
    float spec = sheenColor * D_Charlie(ndoth, rougth) * V_Ashikhman(ndotl, ndotv);
    
    // 布の拡散
    float wrap = saturate((dot(N, L) + 0.5f) / 1.5f);
    float3 diff = albedo * wrap / PI;
    
    return (diff + spec) * lightColor * ndotl;

}

#endif