#include "DebugLineRenderer.hpp"
#include "d3dx12.h"

void DebugLineRenderer::Init(
	const ComPtr<ID3D12Device>& device,
	const ComPtr<ID3D12PipelineState>& linePso)
{
	// デバイス or PSOがぬるぽの場合は処理しない
	if(device == nullptr || linePso == nullptr)
	{
		return;
	}

	// PSOを保存
	m_LinePSO = linePso;

	// 頂点バッファ作成用にメモリを確保
	m_Vertices.reserve(MAX_VERTICES);

	const UINT bufferSize = sizeof(LineVertex) * MAX_VERTICES;

	// uploadヒープで頂点バッファを作成
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	// 失敗時return
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

	void* Mapped = nullptr;
	CD3DX12_RANGE readRange(0, 0); //読み取り専用
	if (FAILED(m_VertexBuffer->Map(0, &readRange, &Mapped)))
	{
		return;
	}

	// マップしたアドレスを頂点バッファ用のポインタに変換
	m_MappedVertexBuffer = reinterpret_cast<LineVertex*>(Mapped);
	m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
	m_VertexBufferView.StrideInBytes = sizeof(LineVertex);
	m_VertexBufferView.SizeInBytes = bufferSize;

	auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(CB_SIZE);
	if(FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_ConstantBuffer.ReleaseAndGetAddressOf()))))
	{
		return;
	}

	// 定数バッファをマップ
	void* cbMapped = nullptr;
	CD3DX12_RANGE cbReadRange(0, 0); //読み取り専用
	if (FAILED(m_ConstantBuffer->Map(0, &cbReadRange, &cbMapped)))
	{
		return;
	}
	m_MappedConstants = reinterpret_cast<std::uint8_t*>(cbMapped);
}

void DebugLineRenderer::Begin()
{
	m_Vertices.clear();
}

void DebugLineRenderer::AddLine(
	const float3& start,
	const float3& end,
	const float4& color)
{
	// 頂点数が上限を超える場合は追加しない
	if (m_Vertices.size() + 2 > MAX_VERTICES)
	{
		return;
	}

	// ラインの始点と終点を頂点バッファに追加
	m_Vertices.push_back({ start, color });
	m_Vertices.push_back({ end, color });
}

void DebugLineRenderer::Draw(const RenderContext& render)
{
	if(render.CommandList == nullptr || m_LinePSO == nullptr)
	{
		return;
	}

	// 頂点バッファに頂点データがない場合も処理しない
	if (m_Vertices.empty())
	{
		return;
	}

	const UINT vertexCount = static_cast<UINT>(m_Vertices.size());
	std::memcpy(m_MappedVertexBuffer, m_Vertices.data(), vertexCount * sizeof(LineVertex));

	LineConstantBuffer constants{};
	// view行列とprojection行列を掛け合わせて定数バッファに保存
	const auto v = DirectX::XMLoadFloat4x4(&render.view);
	const auto p = DirectX::XMLoadFloat4x4(&render.projection);
	const auto vp = v * p;
	DirectX::XMStoreFloat4x4(&constants.viewProj, vp);

	// 定数バッファに行列データをコピー
	std::memcpy(m_MappedConstants, &constants, sizeof(constants));

	render.CommandList->SetPipelineState(m_LinePSO.Get());
	render.CommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	render.CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);


	const auto cbAddress = m_ConstantBuffer->GetGPUVirtualAddress();
	render.CommandList->SetGraphicsRootConstantBufferView(0, cbAddress);

	render.CommandList->DrawInstanced(vertexCount, 1, 0, 0);
}
