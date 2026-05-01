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
	COUNT
};

struct RenderSettings
{
	E_VERTEX_SHADER vertexShader = E_VERTEX_SHADER::BASIC;
	E_PIXEL_SHADER pixelShader = E_PIXEL_SHADER::BASIC;
	bool wireframe = false;

	static RenderSettings& Get()
	{
		static RenderSettings instance;
		return instance;
	}
};

struct RenderContext
{
	ID3D12GraphicsCommandList* CommandList = nullptr;
	float4x4 view{};
	float4x4 projection{};
	bool wireframe = false;
	E_VERTEX_SHADER vertexShader = E_VERTEX_SHADER::BASIC;
	E_PIXEL_SHADER pixelShader = E_PIXEL_SHADER::BASIC;
	UINT frameIndex = 0;
};
