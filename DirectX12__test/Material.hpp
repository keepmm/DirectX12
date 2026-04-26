#pragma once

#include "DirectX.hpp"
#include "Defines.hpp"


class Material
{
public:
	void Init(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const ComPtr<ID3D12PipelineState>& solidPso,
		_In_ const ComPtr<ID3D12PipelineState>& wirePso);

	void SetLightDir(_In_ const float3& dir) { m_LightDir = dir; }
	void SetLightColor(_In_ const float4& color) { m_LightColor = color; }
	void SetAmbientColor(_In_ const float4& color) { m_AmbientColor = color; }

	void Apply(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection,
		bool wireframe);
private:
	struct alignas(256) TransformBuffer
	{
		float4x4 worldViewProj;
		float4x4 world;
		float4 lightDir;
		float4 lightColor;
		float4 ambientColor;
	};

	void UpdateBuffer(
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection) const;

	ComPtr<ID3D12Resource> m_ConstantBuffer;
	ComPtr<ID3D12PipelineState> m_SolidPso;
	ComPtr<ID3D12PipelineState> m_WirePso;

	float3 m_LightDir{ 0.0f,0.0f,-1.0f };
	float4 m_LightColor{ 1.0f,1.0f,1.0f,1.0f };
	float4 m_AmbientColor{ 0.2f,0.2f,0.2f,1.0f };
};

