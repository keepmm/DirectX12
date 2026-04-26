#include "Material.hpp"
#include "d3dx12.h"

void Material::Init(
	const ComPtr<ID3D12Device>& device, 
	const ComPtr<ID3D12PipelineState>& solidPso, 
	const ComPtr<ID3D12PipelineState>& wirePso)
{
	// ˆø”‚Ì‚Ç‚ê‚©‚ª‹ó‚Ìê‡‰Šú‰»‚µ‚È‚¢
	if(device == nullptr) return;

	m_SolidPso = solidPso;
	m_WirePso = wirePso;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(TransformBuffer));

	device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_ConstantBuffer));
}

void Material::Apply(
	ID3D12GraphicsCommandList* commandList, 
	const float4x4& world, 
	const float4x4& view, 
	const float4x4& projection, 
	bool wireframe)
{
	if (commandList == nullptr || m_ConstantBuffer == nullptr) {
		return;
	}

	UpdateBuffer(world, view, projection);

	commandList->SetGraphicsRootConstantBufferView(0, m_ConstantBuffer->GetGPUVirtualAddress());

	if (wireframe) {
		if (m_WirePso != nullptr) {
			commandList->SetPipelineState(m_WirePso.Get());
		}
	}
	else {
		if (m_SolidPso != nullptr) {
			commandList->SetPipelineState(m_SolidPso.Get());
		}
	}
}

void Material::UpdateBuffer(
	const float4x4& world, 
	const float4x4& view, 
	const float4x4& projection) const
{
	TransformBuffer data{};

	const auto w = DirectX::XMLoadFloat4x4(&world);
	const auto v = DirectX::XMLoadFloat4x4(&view);
	const auto p = DirectX::XMLoadFloat4x4(&projection);
	const auto wvp = w * v * p;

	DirectX::XMStoreFloat4x4(&data.worldViewProj, DirectX::XMMatrixTranspose(wvp));
	DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixTranspose(w));

	const auto lightDir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_LightDir));
	DirectX::XMStoreFloat4(&data.lightDir, lightDir);
	data.lightColor = m_LightColor;
	data.ambientColor = m_AmbientColor;

	TransformBuffer* mapped = nullptr;
	const HRESULT hr = m_ConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	if(FAILED(hr)) {
		return;
	}
	*mapped = data;
	m_ConstantBuffer->Unmap(0, nullptr);
}
