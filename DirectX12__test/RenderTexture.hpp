/*****************************************************************//**
 * \file   RenderTexture.hpp
 * \brief  オフスクリーン描画用
 *
 * 作成者
 * 作成日 2026/5/25
 * 更新履歴	5.29 作成
 * *********************************************************************/
#pragma once

#include "d3dx12.h"
#include "Defines.hpp"

class DirectXApp;

class RenderTexture
{
public:
	RenderTexture() = default;
	~RenderTexture() = default;

	RenderTexture(const RenderTexture&) = delete;
	RenderTexture& operator=(const RenderTexture&) = delete;

	/// @brief レンダーテクスチャの初期化
	/// @param device DirectXデバイス
	/// @param width テクスチャの幅
	/// @param height テクスチャの高さ
	/// @param format テクスチャフォーマット(デフォルト適用済み)
	HRESULT Init(
		_In_ DirectXApp& app,
		_In_ UINT width,
		_In_ UINT height,
		_In_ DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

	/// @brief リリースを確保済みスロットを開放
	void Release();

	void Clear(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ const DirectX::XMFLOAT4& clearColor = { 0.0f,0.0f, 0.0f, 1.0f }
	);

	void SetResourceBarrier(
		_In_ ID3D12GraphicsCommandList* commandList,
		_In_ D3D12_RESOURCE_STATES beforeState,
		_In_ D3D12_RESOURCE_STATES afterState
	);

	/// @brief レンダーテクスチャが有効かどうかを取得
	/// @return 有効であれば true、無効であれば false
	bool IsValid() const { return m_Resource != nullptr; }

	inline ComPtr<ID3D12Resource> GetResource() const { return m_Resource; }
	inline D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const { return m_RTVHandle; }
	inline D3D12_GPU_DESCRIPTOR_HANDLE GetSRV() const { return m_SRVHandle; }
	inline D3D12_VIEWPORT GetViewport() const { return m_Viewport; }
	inline D3D12_RECT GetScissorRect() const { return m_ScrissorRect; }

	inline UINT GetWidth() const { return m_Width; }
	inline UINT GetHeight() const { return m_Height; }
private:
	DirectXApp* m_App = nullptr;

	ComPtr<ID3D12Resource> m_Resource;

	// 共有ヒープ内のスロット番号
	UINT m_RTVIndex = UINT_MAX;
	UINT m_SRVIndex = UINT_MAX;

	D3D12_CPU_DESCRIPTOR_HANDLE m_RTVHandle{};
	D3D12_GPU_DESCRIPTOR_HANDLE m_SRVHandle{};

	D3D12_VIEWPORT m_Viewport{};
	D3D12_RECT m_ScrissorRect{};

	UINT m_Width = 0;
	UINT m_Height = 0;
	DXGI_FORMAT m_Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	D3D12_RESOURCE_STATES m_CurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
public:
	void Transition(
		_In_ ID3D12GraphicsCommandList* cmd,
		_In_ D3D12_RESOURCE_STATES after);
	D3D12_RESOURCE_STATES GetState() const { return m_CurrentState; }
};