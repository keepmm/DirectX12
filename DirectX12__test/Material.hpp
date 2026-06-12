#pragma once

#include "DirectX.hpp"
#include "Defines.hpp"
#include <cstdint>
#include "RenderContext.hpp"
#include "DirectXTex/DirectXTex.h"

class ConstantBufferAllocator;

class Material
{
public:
	void Init();

	bool SetTextureFromFile(_In_ const std::wstring& filePath);
	bool SetTextureFromMemory(_In_ const std::uint8_t* data, size_t size);

	void Apply(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection,
		bool wireframe,
		E_VERTEX_SHADER vsType = E_VERTEX_SHADER::BASIC,
		E_PIXEL_SHADER psType = E_PIXEL_SHADER::BASIC,
		_In_opt_ ID3D12PipelineState* overridePso = nullptr,
		UINT frameIndex = 0,
		_In_ ConstantBufferAllocator* cbAlloc = nullptr);

private:
	struct alignas(256) FrameCB
	{
		float4x4 viewProj;
		float4 lightDir;
		float4 lightColor;
		float4 ambientColor;
	};

	struct alignas(256) ObjectCB
	{
		float4x4 world;
	};

	static constexpr UINT FRAME_COUNT = RTV_NUM;
	static constexpr UINT MAX_ENTITY_PER_FRAME = 1024;

	void BuildPerFrame(const float4x4& view, const float4x4& projection, _Out_ FrameCB* out) const;
	void BuildPerObject(const float4x4& world, _Out_ ObjectCB* out) const;

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

	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_TextureFootprint = {};

	UINT m_EntityCountPerFrame[FRAME_COUNT] = {};
	UINT m_LastFrameIndex = UINT_MAX;

	bool UploadTextureData(
		_In_ const DirectX::Image* srcImage,
		_In_ const DirectX::TexMetadata& metadata
	);
};