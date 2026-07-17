#include "BeamRenderer.hpp"
#include "d3dx12.h"

void BeamRenderer::Init(
	const ComPtr<ID3D12Device>& device,
	const ComPtr<ID3D12PipelineState>& beamPso)
{
	if (device == nullptr || beamPso == nullptr)
	{
		return;
	}

	m_BeamPSO = beamPso;

	m_Vertices.reserve(MAX_VERTICES);

	const UINT bufferSize = sizeof(BeamVertex) * MAX_VERTICES * SLOT_NUM;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	if (FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&vbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_VertexBuffer.ReleaseAndGetAddressOf()))))
	{
		return;
	}

	void* mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	if (FAILED(m_VertexBuffer->Map(0, &readRange, &mapped)))
	{
		return;
	}

	m_MappedVertexBuffer = reinterpret_cast<BeamVertex*>(mapped);
	m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
	m_VertexBufferView.StrideInBytes = sizeof(BeamVertex);
	m_VertexBufferView.SizeInBytes = bufferSize;

	auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(CB_SIZE * SLOT_NUM);
	if (FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_ConstantBuffer.ReleaseAndGetAddressOf()))))
	{
		return;
	}

	void* cbMapped = nullptr;
	CD3DX12_RANGE cbReadRange(0, 0);
	if (FAILED(m_ConstantBuffer->Map(0, &cbReadRange, &cbMapped)))
	{
		return;
	}
	m_MappedConstants = reinterpret_cast<std::uint8_t*>(cbMapped);
}

void BeamRenderer::Begin()
{
	m_Vertices.clear();
}

void BeamRenderer::AddTriangle(
	const float3& a,
	const float3& b,
	const float3& c,
	const float4& color)
{
	if (m_Vertices.size() + 3 > MAX_VERTICES)
	{
		return;
	}

	m_Vertices.push_back({ a, color });
	m_Vertices.push_back({ b, color });
	m_Vertices.push_back({ c, color });
}

void BeamRenderer::AddTriangle(
	const float3& a, const float4& colA,
	const float3& b, const float4& colB,
	const float3& c, const float4& colC)
{
	if (m_Vertices.size() + 3 > MAX_VERTICES)
	{
		return;
	}

	m_Vertices.push_back({ a, colA });
	m_Vertices.push_back({ b, colB });
	m_Vertices.push_back({ c, colC });
}

void BeamRenderer::Draw(const RenderContext& render, ID3D12PipelineState* psoOverride)
{
	if (render.CommandList == nullptr || m_BeamPSO == nullptr)
	{
		return;
	}

	if (m_Vertices.empty())
	{
		return;
	}

	const UINT vertexCount = static_cast<UINT>(m_Vertices.size());

	// --- フレームごとに別区画へ書く（GPU読み取り中の上書きを防止）---
	const UINT slot = m_DrawCursor % SLOT_NUM;
	m_DrawCursor++;
	const UINT vbBase = slot * MAX_VERTICES;   // 頂点インデックスのオフセット
	const UINT cbOffset = slot * CB_SIZE;      // 定数バッファのバイトオフセット

	std::memcpy(m_MappedVertexBuffer + vbBase,
		m_Vertices.data(), vertexCount * sizeof(BeamVertex));

	BeamConstantBuffer constants{};
	const auto v = DirectX::XMLoadFloat4x4(&render.view);
	const auto p = DirectX::XMLoadFloat4x4(&render.projection);
	const auto vp = DirectX::XMMatrixTranspose(v * p);
	DirectX::XMStoreFloat4x4(&constants.viewProj, vp);

	std::memcpy(m_MappedConstants + cbOffset, &constants, sizeof(constants));

	// このフレーム区画を指す頂点バッファビュー
	D3D12_VERTEX_BUFFER_VIEW vbv = m_VertexBufferView;
	vbv.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress()
		+ static_cast<UINT64>(vbBase) * sizeof(BeamVertex);
	vbv.SizeInBytes = MAX_VERTICES * sizeof(BeamVertex);

	render.CommandList->SetPipelineState(psoOverride ? psoOverride : m_BeamPSO.Get());
	render.CommandList->IASetVertexBuffers(0, 1, &vbv);
	render.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	render.CommandList->SetGraphicsRootConstantBufferView(0,
		m_ConstantBuffer->GetGPUVirtualAddress() + cbOffset);

	render.CommandList->DrawInstanced(vertexCount, 1, 0, 0);
}