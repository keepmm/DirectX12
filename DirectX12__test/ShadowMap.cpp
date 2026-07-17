#include "ShadowMap.hpp"
#include "d3dx12.h"
#include "DirectX.hpp"

void ShadowMap::Init(UINT size)
{
	auto device = APP->GetDevice();
	m_Size = size;

	CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_TYPELESS, size, size, 1, 1);
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	CD3DX12_CLEAR_VALUE clear(DXGI_FORMAT_D32_FLOAT, 1.0f, 0);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
		IID_PPV_ARGS(m_Texture.GetAddressOf()));

	UINT dsvIndex = 0;
	APP->GetDsvAllocator().Allocate(dsvIndex);
	m_DSV = APP->GetDsvAllocator().Cpu(dsvIndex);

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(m_Texture.Get(), &dsvDesc, m_DSV);

	m_Viewport = { 0, 0, (float)size, (float)size, 0.0f, 1.0f };
	m_Scissor = { 0, 0, (LONG)size, (LONG)size };
}

void ShadowMap::BeginRender(ID3D12GraphicsCommandList* cmd)
{
	auto toDepth = CD3DX12_RESOURCE_BARRIER::Transition(m_Texture.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	cmd->ResourceBarrier(1, &toDepth);

	cmd->OMSetRenderTargets(0, nullptr, FALSE, &m_DSV);   // RT‚È‚µE[“x‚Ì‚Ý
	cmd->ClearDepthStencilView(m_DSV, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	cmd->RSSetViewports(1, &m_Viewport);
	cmd->RSSetScissorRects(1, &m_Scissor);
}

void ShadowMap::EndRender(ID3D12GraphicsCommandList* cmd)
{
	auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(m_Texture.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmd->ResourceBarrier(1, &toSrv);
}