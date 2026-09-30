/*****************************************************************//**
 * \file   D3D12Resource.hpp
 * \brief  IRHITexture / IRHIBuffer ÇÃD3D12 é¿ëï
 * 
 * çÏê¨é“ keepmmm
 * çÏê¨ì˙ 2026/9/30
 * çXêVóöó
 * *********************************************************************/
#pragma once

#include "D3D12Types.hpp"
#include "D3D12DescriptorHeap.hpp"
#include "../IRHITexture.hpp"
#include "../IRHIBuffer.hpp"

class D3D12Device;

class D3D12Texture final : public IRHITexture
{
public:
	D3D12Texture(
		_In_ D3D12Device& device,
		_In_ const RHITextureDesc& desc,
		_In_ ComPtr<ID3D12Resource> resource
	);
	~D3D12Texture() override;

	bool CreateViews();

	const RHITextureDesc& GetDesc()const noexcept override { return m_Desc; }
	void* GetNativeResource() const noexcept override { return m_Resource.Get(); }
private:
	D3D12Device& m_Device;
	ComPtr<ID3D12Resource> m_Resource;
	RHITextureDesc m_Desc;

	uint32_t m_Srvindex = D3D12DescriptorHeap::INVALID_INDEX;
	uint32_t m_Rtvindex = D3D12DescriptorHeap::INVALID_INDEX;
	uint32_t m_Dsvindex = D3D12DescriptorHeap::INVALID_INDEX;
	D3D12_CPU_DESCRIPTOR_HANDLE m_Srv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Rtv{};
	D3D12_CPU_DESCRIPTOR_HANDLE m_Dsv{};
};
