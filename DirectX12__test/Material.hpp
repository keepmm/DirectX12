#pragma once

#include "DirectX.hpp"
#include "Defines.hpp"
#include <cstdint>
#include "RenderContext.hpp"
#include <DirectXTex.h>
#include "ShaderTypes.hpp"

class ConstantBufferAllocator;

class Material
{
public:
	/// @brief 初期化
	void Init();

	/*
	*	テクスチャの設定
	*/
	bool SetTextureFromFile(_In_ const std::wstring& filePath);

	/** 
	*	メモリからテクスチャを設定
	*/
	bool SetTextureFromMemory(_In_ const std::uint8_t* data, size_t size);

	/// @brief トゥーンラップテクスチャの設定
	bool SetToonRampTexture(_In_ const std::wstring& filepath);

	void ShareDiffuseTexture(_In_ const Material& src);

	void Apply(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ const float4x4& world,
		_In_ const float4x4& view,
		_In_ const float4x4& projection,
		bool wireframe,
		UINT frameIndex = 0,
		_In_ ConstantBufferAllocator* cbAlloc = nullptr,
		_In_ std::string shaderName = "",
		_In_opt_ ID3D12PipelineState* overridePso = nullptr);
		
	void UpdateTextureIfNeeded(_In_ ID3D12GraphicsCommandList* commandList);

	bool CreateTextureFromRGBA(
		_In_ UINT Width,
		_In_ UINT Height,
		_In_ const std::uint8_t* data
	);

	bool CreateMapFromRGBA(
		UINT srvSlot,
		_In_ UINT Width,
		_In_ UINT Height,
		_In_ const std::uint8_t* data,
		_Inout_ ComPtr<ID3D12Resource>& outTexture,
		_Inout_ ComPtr<ID3D12Resource>& outUpload,
		_Out_ D3D12_PLACED_SUBRESOURCE_FOOTPRINT& outFootPrint,
		_Out_ bool& outPending
	);
	bool CreateNormalFromRGBA(UINT w, UINT h, const std::uint8_t* p);
	bool CreateMetalFromRGBA(UINT w, UINT h, const std::uint8_t* p);
	bool CreateRoughFromRGBA(UINT w, UINT h, const std::uint8_t* p);
public:
	float roughness = 0.5f;
	float metallic = 0.0f;
	float4 rimColor = { 1.0f,1.0f,1.0f,1.0f };
	bool isFace = false;
	float outlineWidth = 1.0f;
	float sssStrength = 0.0f;	// 肌 : 0.5 ~ 0.7
	float sssWrap = 0.4f;		// 明暗境界のなだらかさ
	float sssTrans = 0.0f;		// 耳・指の逆光透過
	float sheen = 0.0f;			// 布 : 0.5 ~ 1.0
	COLOR sssColor = {0.9f,0.35f,0.25f,1.0f};
	float baseAlpha = 1.0f;
	COLOR baseColor = { 1.0f,1.0f,1.0f,1.0f };
	std::string shaderName;

	bool SetNormalTexture(_In_ const std::wstring& path);
	bool SetMetalTexture(_In_ const std::wstring& path);
	bool SetRoughTexture(_In_ const std::wstring& path);

	// SRVヒープを5枚で確保＋全slotをデフォルト充填（重複コード集約）
	bool EnsureSrvHeap();
private:
	static constexpr UINT FRAME_COUNT = RTV_NUM;
	static constexpr UINT MAX_ENTITY_PER_FRAME = 1024;

	void BuildPerFrame(const float4x4& view, const float4x4& projection, _Out_ FrameCB* out) const;
	void BuildPerObject(const float4x4& world, _Out_ ObjectCB* out) const;

	void CreateCheckerTexture(_In_ const ComPtr<ID3D12Device>& device);

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

	bool UploadTextureTo(
		_In_ const DirectX::Image* srcImage,
		_In_ const DirectX::TexMetadata& metadata,
		UINT srvSlot,
		_Inout_ ComPtr<ID3D12Resource>& outTexture,
		_Inout_ ComPtr<ID3D12Resource>& outUpload,
		_Out_ D3D12_PLACED_SUBRESOURCE_FOOTPRINT& outFootPrint,
		_Out_ bool& outPending
	);

	ComPtr<ID3D12Resource> m_RampTexture;
	ComPtr<ID3D12Resource> m_RampUpload;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_RampFootprint = {};
	bool m_RampUploadPending = false;

	void CreateDefaultRampTexture();

	ComPtr<ID3D12Resource> m_NormalTexture, m_NormalUpload;
	ComPtr<ID3D12Resource> m_MetalTexture, m_MetalUpload;
	ComPtr<ID3D12Resource> m_RoughTexture, m_RoughUpload;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_NormalFootprint = {}, m_MetalFootprint = {}, m_RoughFootprint = {};
	bool m_NormalPending = false, m_MetalPending = false, m_RoughPending = false;
	bool m_HasNormal = false, m_HasMetal = false, m_HasRough = false;

	UINT m_UploadFenceValue = 0;

	bool m_EnvBound = false;
	float m_EnvMaxMip = 0.0f;
	void BindEnvironmentIfNeeded();
	void BindShadowMapIfNeeded();
	bool m_ShadowBound = false;

};