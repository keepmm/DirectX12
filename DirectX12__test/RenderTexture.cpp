/*****************************************************************//**
 * \file   RenderTexture.cpp
 * \brief  オフスクリーン描画用
 *
 * 作成者
 * 作成日 2026/5/25
 * 更新履歴 5.29 実装
 * *********************************************************************/
#include "RenderTexture.hpp"
#include "DirectX.hpp"
#include "Logger.hpp"

HRESULT RenderTexture::Init(
	_In_ DirectXApp& app,
	_In_ UINT width,
	_In_ UINT height,
	_In_ DXGI_FORMAT format)
{
	// -------------------------------//
	//				初期化			  //
	// -------------------------------//

	// サイズ0ガード
	if (width == 0 || height == 0)
	{
		return E_INVALIDARG;
	}

	// 作り直し対応
	Release();
	
	m_App = &app;
	m_Width = width;
	m_Height = height;
	m_Format = format;

	ID3D12Device& device = *app.GetDevice().Get();

	// -----------------------//
	//		リソースの記述	  //
	// -----------------------//
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension				= D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width				= width;
	resourceDesc.Height				= height;
	resourceDesc.DepthOrArraySize	= 1;
	resourceDesc.MipLevels			= 1;
	resourceDesc.Format				= format;
	resourceDesc.SampleDesc.Count	= 1;
	resourceDesc.SampleDesc.Quality	= 0;
	resourceDesc.Flags				= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	resourceDesc.Layout				= D3D12_TEXTURE_LAYOUT_UNKNOWN;

	// リソースの初期状態
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = format;
	clearValue.Color[0] = 0.0f;
	clearValue.Color[1] = 0.5f;
	clearValue.Color[2] = 0.7f;
	clearValue.Color[3] = 1.0f;

	// リソースの生成
	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

	HRESULT hr = device.CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(m_Resource.ReleaseAndGetAddressOf()));
	if (FAILED(hr))
	{
		return hr;
	}

	// ------------------------------ //
	// RTV専用ヒープからスロット確保  //
	// ------------------------------ //
	if(!app.GetRtvAllocator().Allocate(m_RTVIndex))
	{
		// 確保できなかった場合はリソースを解放して終了
		Release();
		return E_OUTOFMEMORY;
	}
	// RTV ハンドルの取得
	m_RTVHandle = app.GetRtvAllocator().Cpu(m_RTVIndex);
	// RTV の生成
	device.CreateRenderTargetView(m_Resource.Get(), nullptr, m_RTVHandle);

	// ------------------------------ //
	// SRV専用ヒープからスロット確保  //
	// ------------------------------ //
	if(!app.GetSrvAllocator().Allocate(m_SRVIndex))
	{
		// 確保できなかった場合はリソースを解放して終了
		Release();
		return E_OUTOFMEMORY;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	device.CreateShaderResourceView(
		m_Resource.Get(),&srvDesc,app.GetSrvAllocator().Cpu(m_SRVIndex));
	m_SRVHandle = app.GetSrvAllocator().Gpu(m_SRVIndex);

	// -------------------------------------//
	// ビューポートの設定・シザー矩形の設定 //
	// -------------------------------------//
	m_Viewport.TopLeftX		= 0.0f;
	m_Viewport.TopLeftY		= 0.0f;
	m_Viewport.Width		= static_cast<float>(width);
	m_Viewport.Height		= static_cast<float>(height);
	m_Viewport.MinDepth		= 0.0f;
	m_Viewport.MaxDepth		= 1.0f;
	m_ScrissorRect.left		= 0;
	m_ScrissorRect.top		= 0;
	m_ScrissorRect.right	= width;
	m_ScrissorRect.bottom	= height;

	m_CurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	m_Resource->SetName(L"RT_Game");

	return S_OK;
}

void RenderTexture::Clear(
	_In_ ID3D12GraphicsCommandList* commandList,
	_In_ const DirectX::XMFLOAT4& clearColor)
{
	if (!commandList || !m_Resource)
	{
		return;
	}

	// レンダーターゲットをクリア
	commandList->ClearRenderTargetView(
		m_RTVHandle,
		reinterpret_cast<const float*>(&clearColor),
		0,
		nullptr);
}

void RenderTexture::Release()
{
	// 確保済みスロットをアロケータへ返す(GPUアイドル前提で呼ばれる)
	if (m_App)
	{
		if (m_RTVIndex != UINT_MAX)
		{
			m_App->GetRtvAllocator().Free(m_RTVIndex);
		}
		if (m_SRVIndex != UINT_MAX)
		{
			m_App->GetSrvAllocator().Free(m_SRVIndex);
		}
	}

	m_RTVIndex = UINT_MAX;
	m_SRVIndex = UINT_MAX;
	m_SRVHandle = {};
	m_RTVHandle = {};
	m_Resource.Reset();
}

void RenderTexture::SetResourceBarrier(
	_In_ ID3D12GraphicsCommandList* commandList,
	_In_ D3D12_RESOURCE_STATES beforeState,
	_In_ D3D12_RESOURCE_STATES afterState)
{
	if (!commandList || !m_Resource)
	{
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = m_Resource.Get();
	barrier.Transition.StateBefore = beforeState;
	barrier.Transition.StateAfter = afterState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	commandList->ResourceBarrier(1, &barrier);
}

void RenderTexture::Transition(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after)
{
	if (!cmd || !m_Resource) return;
	// 同じ状態なら何もしない
	if (m_CurrentState == after) return;

	D3D12_RESOURCE_BARRIER b{};
	b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition.pResource = m_Resource.Get();
	b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	b.Transition.StateBefore = m_CurrentState;	// 実際の現在状態を使う
	b.Transition.StateAfter = after;
	cmd->ResourceBarrier(1, &b);

	//LOG->LogInfo("Trans res=" + std::to_string((uintptr_t)m_Resource.Get()) +
	//	" cur=" + std::to_string(m_CurrentState) + " ->" + std::to_string(after) +
	//	(m_CurrentState == after ? " SKIP" : " EMIT"));

	m_CurrentState = after;	// 追跡更新
}
