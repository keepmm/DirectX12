#pragma once

#include "Defines.hpp"
#include <d3d12.h>

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

	RenderTexture* viewportRenderTexture = nullptr;
	D3D12_VIEWPORT* viewport = nullptr;
	D3D12_RECT* scissorRect = nullptr;

	D3D12_CPU_DESCRIPTOR_HANDLE depthStencilView = {};
};
