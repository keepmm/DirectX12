#pragma once

#include "Defines.hpp"
#include <d3d12.h>

struct RenderContext
{
	ID3D12GraphicsCommandList* CommandList = nullptr;
	float4x4 view{};
	float4x4 projection{};
	bool wireframe = false;
};
