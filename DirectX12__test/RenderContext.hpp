#pragma once

#include "Defines.hpp"
#include <d3d12.h>

constexpr UINT MAX_LIGHTS = 10;

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
	LightData lights[MAX_LIGHTS];
};

enum class E_VERTEX_SHADER
{
	BASIC,
	COUNT
};

enum class E_PIXEL_SHADER
{
	BASIC,
	TOON,
	EMISSIVE,
	COUNT
};

struct RenderSettings
{
	E_VERTEX_SHADER vertexShader = E_VERTEX_SHADER::BASIC;
	E_PIXEL_SHADER pixelShader = E_PIXEL_SHADER::BASIC;
	bool wireframe = false;
	bool meshShader = false;

	static RenderSettings& Get()
	{
		static RenderSettings instance;
		return instance;
	}
};

class RenderTexture;
class ConstantBufferAllocator;

struct RenderContext
{
	ID3D12GraphicsCommandList* CommandList = nullptr;
	ID3D12GraphicsCommandList6* CommandList6 = nullptr;
	ID3D12PipelineState* meshShaderPso = nullptr;

	float4x4 view{};
	float4x4 projection{};

	bool wireframe = false;
	bool useMeshShader = false;
	bool meshShaderSupported = false;

	bool isSceneView = false;

	E_VERTEX_SHADER vertexShader = E_VERTEX_SHADER::BASIC;
	E_PIXEL_SHADER pixelShader = E_PIXEL_SHADER::BASIC;
	UINT frameIndex = 0;

	ConstantBufferAllocator* cbAllocator = nullptr;
	LightCB lightCb;

	RenderTexture* viewportRenderTexture = nullptr;
	D3D12_VIEWPORT* viewport = nullptr;
	D3D12_RECT* scissorRect = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = {};
};
