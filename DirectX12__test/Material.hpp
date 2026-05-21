#pragma once

#include "DirectX.hpp"
#include "Defines.hpp"
#include <cstdint>
#include "RenderContext.hpp"

class Material
{
public:
	void Init(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const DirectXApp::PipelineStateTable& pipelinestates,
		_In_ const ComPtr<ID3D12PipelineState>& wirePso);

	bool SetTextureFromFile(_In_ const std::wstring& filePath);
	bool SetTextureFromMemory(_In_ const std::uint8_t* data, size_t size);

	void SetLightDir(_In_ const float3& dir) { m_LightDir = dir; }
	void SetLightColor(_In_ const float4& color) { m_LightColor = color; }
	void SetAmbientColor(_In_ const float4& color) { m_AmbientColor = color; }

	void Apply(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection,
		bool wireframe,
		E_VERTEX_SHADER vsType = E_VERTEX_SHADER::BASIC,
		E_PIXEL_SHADER psType = E_PIXEL_SHADER::BASIC,
		_In_opt_ ID3D12PipelineState* overridePso = nullptr,
		UINT frameIndex = 0);

private:
	struct alignas(256) TransformBuffer
	{
		float4x4 worldViewProj;
		float4x4 world;
		float4 lightDir;
		float4 lightColor;
		float4 ambientColor;
	};

	static constexpr UINT FRAME_COUNT = DirectXApp::RTV_NUM;
	static constexpr UINT MAX_ENTITY_PER_FRAME = 1024;
	static constexpr UINT CB_SIZE = (sizeof(TransformBuffer) + 255u) & ~255u;

	void BuildBufferData(
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection,
		_Out_ TransformBuffer* outData) const;

	void CreateCheckerTexture(_In_ const ComPtr<ID3D12Device>& device);
	void UpdateTextureIfNeeded(_In_ ID3D12GraphicsCommandList* commandList);

	ComPtr<ID3D12Resource> m_ConstantBuffer[FRAME_COUNT];
	std::uint8_t* m_MappedData[FRAME_COUNT] = {};

	DirectXApp::PipelineStateTable m_PipelineStates;
	ComPtr<ID3D12PipelineState> m_WirePso;

	ComPtr<ID3D12DescriptorHeap> m_TextureSrvHeap;
	ComPtr<ID3D12Resource> m_Texture;
	ComPtr<ID3D12Resource> m_TextureUpload;
	bool m_TextureUploadPending = false;

	ComPtr<ID3D12Device> m_Device;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_TextureFootprint = {};

	UINT m_EntityCountPerFrame[FRAME_COUNT] = {};
	UINT m_LastFrameIndex = UINT_MAX;

	float3 m_LightDir{ 0.0f,0.0f,-1.0f };
	float4 m_LightColor{ 1.0f,1.0f,1.0f,1.0f };
	float4 m_AmbientColor{ 0.2f,0.2f,0.2f,1.0f };
};