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

// --- Rim (band shaped) ---
// width    : 0 = narrow band on the silhouette, 1 = covers the whole surface
// softness : 0 = hard edge, 1 = wide gradient
// Keeps the highlight on the silhouette only, so the model does not look
// wrapped in a glowing outline like pow(fresnel, n) does.
float RimBand(float3 N, float3 V, float width, float softness)
{
    float f = 1.0f - saturate(dot(N, V));
    float edge = saturate(1.0f - width);
    float soft = max(softness * 0.5f, 1e-3f);
    return smoothstep(edge - soft, edge + soft, f);
}

// Rim mask driven by a light direction.
// amount 0 : rim is applied uniformly (old behaviour)
// amount 1 : rim only appears on the side the light comes from
float RimLightMask(float3 N, float3 L, float amount)
{
    return lerp(1.0f, smoothstep(0.0f, 0.35f, dot(N, L)), saturate(amount));
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

#endif