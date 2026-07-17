#pragma once

#include "Defines.hpp"
#include <d3d12.h>
#include "ShaderTypes.hpp"

struct ShaderPassDef
{
	std::wstring vsFile = L"VertexShader.hlsl";
	std::string vsEntry = "BasicVS";
	std::string vsProfile;
	std::wstring psFile;
	std::string psEntry;
	std::string psProfile = "ps_5_0";
	bool alphaBlend = false;
	D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_BACK;
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
	PBR,
	COUNT
};

struct RenderSettings
{
	E_VERTEX_SHADER vertexShader = E_VERTEX_SHADER::BASIC;
	E_PIXEL_SHADER pixelShader = E_PIXEL_SHADER::BASIC;
	bool wireframe = false;
	bool meshShader = false;
	bool deferred = false;

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
