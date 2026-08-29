/*****************************************************************//**
 * \file   ShadowMap.hpp
 * \brief  シャドウマッピング
 * 
 * 作成者 keeeep
 * 作成日 2026/7/3
 * 更新履歴	7.3 作成
 * *********************************************************************/
#pragma once

#include <d3d12.h>
#include "Defines.hpp"

class ShadowMap
{
public:
	void Init(
		_In_ const UINT size = 2048
	);

	void BeginRender(_In_ ID3D12GraphicsCommandList* cmd);
	void EndRender(_In_ ID3D12GraphicsCommandList* cmd);

	ID3D12Resource* GetResource() const noexcept { return m_Texture.Get(); }
	UINT GetSize() const noexcept { return m_Size; }
private:
	ComPtr<ID3D12Resource> m_Texture;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DSV;
	UINT m_Size = 2048;
	D3D12_VIEWPORT m_Viewport;
	D3D12_RECT m_Scissor{};
};

