#include "Polygon.hpp"
#include "d3dx12.h"
#include "Vertex.hpp"

constexpr float _Color = 0.6f;
constexpr float4 PolygonColor = { _Color,_Color,_Color, 1.0f };

float3 Polygon::m_LightDir = { 0.0f, 0.0f, -1.0f };
float4 Polygon::m_LightColor = { 1.0f, 1.0f, 1.0f, 1.0f };
float4 Polygon::m_AmbientColor = { 0.2f, 0.2f, 0.2f, 1.0f };

float4x4					Polygon::m_wvp[3];
ComPtr<ID3D12Resource>		Polygon::m_VertexBuffer;
D3D12_VERTEX_BUFFER_VIEW	Polygon::m_VertexBufferView;
ComPtr<ID3D12Resource>		Polygon::m_IndexBuffer;
D3D12_INDEX_BUFFER_VIEW		Polygon::m_IndexBufferView;
ComPtr<ID3D12Resource>		Polygon::m_ConstantBuffer;
ComPtr<ID3D12Device>		Polygon::m_Device;
ComPtr<ID3D12GraphicsCommandList> Polygon::m_CommandList;
ComPtr<ID3D12PipelineState> Polygon::m_PipelineState;
ComPtr<ID3D12PipelineState> Polygon::m_PipelineStateWireFrame;
bool Polygon::m_IsDrawWireFrame = false;
bool Polygon::m_isDrawPolygon = true;

struct alignas(256) Transform
{
	float4x4 worldViewProj;
	float4x4 world;
	float4 lightDir;
	float4 lightColor;
	float4 ambientColor;
};

Polygon::~Polygon()
{
}

void Polygon::Init(ComPtr<ID3D12Device> device, ComPtr<ID3D12GraphicsCommandList> commandList,
	ComPtr<ID3D12PipelineState> pipelineState, ComPtr<ID3D12PipelineState> pipelineStateWireFrame)
{
	/* 初期化 */
	m_Device = device;
	m_CommandList = commandList;
	m_PipelineState = pipelineState;
	m_PipelineStateWireFrame = pipelineStateWireFrame;
	for (int i = 0; i < 3; ++i) {
		DirectX::XMStoreFloat4x4(&m_wvp[i], DirectX::XMMatrixIdentity());
	}
}

void Polygon::CreatePolygon()
{
	// alignas ... DirectX12の定数バッファ要求に合わすためのアライメント
	struct alignas(256) Transform
	{
		DirectX::XMFLOAT4X4 worldViewProj;
	};

	Vertex vertices[] =
	{
		{ { -0.5f,  0.5f, -0.5f},{0.0f, 0.0f, -1.0f},PolygonColor },
		{ {  0.5f,  0.5f, -0.5f},{0.0f, 0.0f, -1.0f},PolygonColor },
		{ { -0.5f, -0.5f, -0.5f},{0.0f, 0.0f, -1.0f},PolygonColor },
		{ {  0.5f, -0.5f, -0.5f},{0.0f, 0.0f, -1.0f},PolygonColor },

		// +Z面 (Front)
		{ { 0.5f,  0.5f,  0.5f},{0.0f,0.0f,1.0f}, PolygonColor },
		{ {-0.5f,  0.5f,  0.5f},{0.0f,0.0f,1.0f}, PolygonColor },
		{ { 0.5f, -0.5f,  0.5f},{0.0f,0.0f,1.0f}, PolygonColor },
		{ {-0.5f, -0.5f,  0.5f},{0.0f,0.0f,1.0f}, PolygonColor },

		// -X面 (Left)
		{ {-0.5f,  0.5f,  0.5f}, {-1.0f,0.0f,0.0f}, PolygonColor },
		{ {-0.5f,  0.5f, -0.5f}, {-1.0f,0.0f,0.0f}, PolygonColor },
		{ {-0.5f, -0.5f,  0.5f}, {-1.0f,0.0f,0.0f}, PolygonColor },
		{ {-0.5f, -0.5f, -0.5f}, {-1.0f,0.0f,0.0f}, PolygonColor },

		// +X面 (Right)
		{ { 0.5f,  0.5f, -0.5f}, {1.0f,0.0f,0.0f}, PolygonColor },
		{ { 0.5f,  0.5f,  0.5f}, {1.0f,0.0f,0.0f}, PolygonColor },
		{ { 0.5f, -0.5f, -0.5f}, {1.0f,0.0f,0.0f}, PolygonColor },
		{ { 0.5f, -0.5f,  0.5f}, {1.0f,0.0f,0.0f}, PolygonColor },

		// +Y面 (Top)
		{ { -0.5f,  0.5f,  0.5f},{0.0f,1.0f,0.0f}, PolygonColor },
		{ {  0.5f,  0.5f,  0.5f},{0.0f,1.0f,0.0f}, PolygonColor },
		{ { -0.5f,  0.5f, -0.5f},{0.0f,1.0f,0.0f}, PolygonColor },
		{ {  0.5f,  0.5f, -0.5f},{0.0f,1.0f,0.0f}, PolygonColor },

		// -Y面 (Bottom)	
		{ { 0.5f, -0.5f,  0.5f}, {0.0f,-1.0f,0.0f}, PolygonColor },
		{ {-0.5f, -0.5f,  0.5f}, {0.0f,-1.0f,0.0f}, PolygonColor },
		{ { 0.5f, -0.5f, -0.5f}, {0.0f,-1.0f,0.0f}, PolygonColor },
		{ {-0.5f, -0.5f, -0.5f}, {0.0f,-1.0f,0.0f}, PolygonColor },
	};

	int index[] = {
		0,1,2, 1,3,2,			// -Z面
		4,5,6, 5,7,6,			// +Z面
		8,9,10, 9,11,10,		// -X面
		12,13,14, 13,15,14,		// +X面
		16,17,18, 17,19,18,		// +Y面
		20,21,22, 21,23,22		// -Y面
	};

	// インデックスバッファを作成
	CD3DX12_HEAP_PROPERTIES HeapProp(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(index));
	m_Device->CreateCommittedResource(
		&HeapProp,
		D3D12_HEAP_FLAG_NONE,
		&ResourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(m_IndexBuffer.GetAddressOf())
	);

	void* pIndexData = nullptr;
	m_IndexBuffer->Map(0, nullptr, &pIndexData);
	memcpy(pIndexData, index, sizeof(index));
	m_IndexBuffer->Unmap(0, nullptr);

	m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
	m_IndexBufferView.SizeInBytes = sizeof(index);
	m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;

	// 定数バッファ用データ初期化
	Transform transformData{};

	// 定数バッファ作成(CPUがアップロード可能)
	CD3DX12_HEAP_PROPERTIES constantHeapProp(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC  constantResouceDesc(CD3DX12_RESOURCE_DESC::Buffer(sizeof(Transform)));
	m_Device->CreateCommittedResource(
		&constantHeapProp,
		D3D12_HEAP_FLAG_NONE,
		&constantResouceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_ConstantBuffer)
	);

	// 定数バッファにデータをマッピング
	Transform* MappedData = nullptr;
	m_ConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&MappedData));
	*MappedData = transformData;
	m_ConstantBuffer->Unmap(0, nullptr);

	// 頂点バッファの作成
	const UINT vbSize = sizeof(vertices);
	CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vbSize);

	m_Device->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&m_VertexBuffer)
	);

	//　頂点バッファにデータをコピー
	UINT8* pVertexDataBegin = nullptr;
	CD3DX12_RANGE readRange(0, 0);	// 読み込みなし
	m_VertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin));
	memcpy(pVertexDataBegin, vertices, vbSize);
	m_VertexBuffer->Unmap(0, nullptr);

	// 頂点バッファビューの設定
	m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();	// 仮想のGPUデータアドレスを送信
	m_VertexBufferView.StrideInBytes = sizeof(Vertex);
	m_VertexBufferView.SizeInBytes = vbSize;
}

void Polygon::SetWorld(float4x4 world)
{
	m_wvp[0] = world;
}

void Polygon::SetView(float4x4 view)
{
	m_wvp[1] = view;
}

void Polygon::SetProjection(float4x4 proj)
{
	m_wvp[2] = proj;
}

void Polygon::DrawWireFrame(bool b)
{
	m_IsDrawWireFrame = b;
}

void Polygon::DrawPolygon(bool b)
{
	m_isDrawPolygon = b;
}

void Polygon::Draw()
{
	// 行列を定数バッファに送信
	Transform transformData{};
	const auto world = DirectX::XMLoadFloat4x4(&m_wvp[0]);
	const auto wvp =
		world *
		DirectX::XMLoadFloat4x4(&m_wvp[1]) *
		DirectX::XMLoadFloat4x4(&m_wvp[2]);

	DirectX::XMStoreFloat4x4(&transformData.worldViewProj, DirectX::XMMatrixTranspose(wvp));
	DirectX::XMStoreFloat4x4(&transformData.world, DirectX::XMMatrixTranspose(world));
	const auto lightDir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&m_LightDir));
	DirectX::XMStoreFloat4(&transformData.lightDir, lightDir);
	const auto lightColor = DirectX::XMVector3Normalize(DirectX::XMLoadFloat4(&m_LightColor));
	DirectX::XMStoreFloat4(&transformData.lightColor, lightColor);
	const auto ambientColor = DirectX::XMVector3Normalize(DirectX::XMLoadFloat4(&m_AmbientColor));
	DirectX::XMStoreFloat4(&transformData.ambientColor, ambientColor);

	Transform* MappedData = nullptr;
	m_ConstantBuffer->Map(0, nullptr, reinterpret_cast<void**>(&MappedData));
	*MappedData = transformData;
	m_ConstantBuffer->Unmap(0, nullptr);


	/* コマンドリストが無効な場合は何もしない */
	if (!m_CommandList) return;

	// GPUに定数バッファを送信
	m_CommandList->SetGraphicsRootConstantBufferView(
		0,
		m_ConstantBuffer->GetGPUVirtualAddress()
	);

	/* 頂点 / インデックスが作成されていればバインドして描画する */
	if (m_VertexBuffer) {
		m_CommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	}
	if (m_IndexBuffer) {
		m_CommandList->IASetIndexBuffer(&m_IndexBufferView);
		m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (m_isDrawPolygon) {
			m_CommandList->SetPipelineState(m_PipelineState.Get());
			m_CommandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
		}

		// ワイヤーフレームで描画
		if (m_IsDrawWireFrame) {
			m_CommandList->SetPipelineState(m_PipelineStateWireFrame.Get());
			m_CommandList->DrawIndexedInstanced(36, 1, 0, 0, 0);
		}
	}
}
