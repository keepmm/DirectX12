#pragma once

#include "Defines.hpp"

constexpr UINT MAX_LIGHTS = 10;

struct alignas(256) DeferredCB
{
	float4x4 invViewProj;
	float4 envParam;
};

struct alignas(256) FrameCB
{
	float4x4 viewProj;
	float4 cameraPos;
};

struct alignas(256) ObjectCB
{
	float4x4 world;
};

struct alignas(256) MaterialCB
{
	float roughness;
	float metallic;
	float2 _pad;
	float4 rimColor;
	float4 mapFlags; // x: hasNormal, y: hasMetal, z: hasRough
	float4 faceParam; // x isFace w Outline
	float4 sssParams; // x = SSS強度 y = ラップ量 z = 逆光透過 w = 布シーン強度
	float4 sssColor;  // x = SSS色R y = SSS色G z = SSS色B
	float4 basecolor;

};

struct LightData
{
	float4 dir{ 0.0f, -1.0f, 0.0f, 0.0f };		// xyz: 方向（正規化済み）
	float4 color{ 1.0f, 1.0f, 1.0f, 1.0f };	// rgb: 色 × 強度
	float4 posRange{ 0.0f, 0.0f, 0.0f, 10.0f };	// xyz: 位置, w: 範囲
	float4 param{ 0.0f, 0.0f, 0.0f, 0.0f };	// x: タイプ, y: cos(スポット半角)
};

struct alignas(256) LightCB
{
	float4 ambientColor{ 0.1f, 0.1f, 0.1f, 1.0f };
	float4 lightCount{ 0.0f, 0.0f, 0.0f, 0.0f };	// x: 有効ライト数
	float4x4 lightviewproj{};
	float4 shadowParams{ 0.002f, 0.0f, 2048.0f, 0.0f };
	LightData lights[MAX_LIGHTS];
};

constexpr UINT MAX_BONES = 512;

struct alignas(256) BoneCB
{
	float4x4 boneMatrices[MAX_BONES];
	float3 pad{ 0.0f,0.0f,0.0f };
	float morph;
};

struct alignas(256) PostCB
{
	float2 texelSize;
	float threshold;
	float intensity;
};
