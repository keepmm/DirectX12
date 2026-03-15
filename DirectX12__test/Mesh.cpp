#include "Mesh.hpp"
#include "d3dx12.h"
#include <array>
#include <cstdint>
#include <cstring>

namespace
{
	constexpr float C = 0.6f;
	constexpr float4 Color = { C, C, C, 1.0f };
}

void Mesh::Init(
	const ComPtr<ID3D12Device>& device,
	const std::vector<Vertex>& vertices,
	const std::vector<std::uint32_t>& indices,
	const std::vector<SubMesh>* subMeshes)
{
	// 引数のどれかが空の場合初期化しない
	if(device == nullptr || vertices.empty() || indices.empty())
	{
		return;
	}

	const UINT vertexBufferSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
	const UINT indexBufferSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

	// 頂点バッファの作成
		CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

	{
		auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
		device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&vbDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_VertexBuffer));

		void* vbMapped = nullptr;
		CD3DX12_RANGE readRange(0, 0); // 読み取りはしない
		m_VertexBuffer->Map(0, &readRange, &vbMapped);
		std::memcpy(vbMapped, vertices.data(), vertexBufferSize);
		m_VertexBuffer->Unmap(0, nullptr);

		m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
		m_VertexBufferView.StrideInBytes = sizeof(Vertex);
		m_VertexBufferView.SizeInBytes = vertexBufferSize;
	}

	{
		auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
		device->CreateCommittedResource(
			&uploadHeap,
			D3D12_HEAP_FLAG_NONE,
			&ibDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_IndexBuffer));
		void* ibMapped = nullptr;
		CD3DX12_RANGE readRange(0, 0); // 読み取りはしない
		m_IndexBuffer->Map(0, &readRange, &ibMapped);
		std::memcpy(ibMapped, indices.data(), indexBufferSize);
		m_IndexBuffer->Unmap(0, nullptr);
		m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
		m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
		m_IndexBufferView.SizeInBytes = indexBufferSize;
	}

	m_IndexCount = static_cast<UINT>(indices.size());

	m_SubMeshes.clear();
	if(subMeshes != nullptr && !subMeshes->empty())
	{
		m_SubMeshes = *subMeshes;
	}
	else
	{
		m_SubMeshes.emplace_back(SubMesh{ 0, m_IndexCount, 0 });
	}
}

void Mesh::CreateCube(const ComPtr<ID3D12Device>& device)
{
	const std::array<Vertex, 24> vertices =
	{
		Vertex{ { -0.5f,  0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f, -0.5f, -0.5f },{ 0.0f, 0.0f, -1.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ {  0.5f,  0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ { -0.5f, -0.5f,  0.5f },{ 0.0f, 0.0f,  1.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ { -0.5f,  0.5f,  0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f, -0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f,  0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ { -0.5f, -0.5f, -0.5f },{ -1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ {  0.5f,  0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f,  0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f, -0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f, -0.5f,  0.5f },{ 1.0f, 0.0f, 0.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ { -0.5f,  0.5f,  0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ {  0.5f,  0.5f,  0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ { -0.5f,  0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ {  0.5f,  0.5f, -0.5f },{ 0.0f, 1.0f, 0.0f }, Color,{ 1.0f, 1.0f } },

		Vertex{ {  0.5f, -0.5f,  0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 0.0f, 0.0f } },
		Vertex{ { -0.5f, -0.5f,  0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 1.0f, 0.0f } },
		Vertex{ {  0.5f, -0.5f, -0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 0.0f, 1.0f } },
		Vertex{ { -0.5f, -0.5f, -0.5f },{ 0.0f, -1.0f, 0.0f }, Color,{ 1.0f, 1.0f } }
	};

	const std::array<std::uint32_t, 36> indices =
	{
		0,1,2, 1,3,2,
		4,5,6, 5,7,6,
		8,9,10, 9,11,10,
		12,13,14, 13,15,14,
		16,17,18, 17,19,18,
		20,21,22, 21,23,22
	};

	std::vector<Vertex> vb(vertices.begin(), vertices.end());
	std::vector<std::uint32_t> ib(indices.begin(), indices.end());
	Init(device, vb, ib, nullptr);
}

void Mesh::Draw(ID3D12GraphicsCommandList* commandList) const
{
	if (commandList == nullptr || m_IndexCount == 0) {
		return;
	}

	commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	commandList->IASetIndexBuffer(&m_IndexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);
}

void Mesh::DrawSubMesh(ID3D12GraphicsCommandList* commandList, UINT subMeshIndex) const
{
	if(commandList == nullptr || subMeshIndex >= m_SubMeshes.size())
	{
		return;
	}

	const auto& sm = m_SubMeshes[subMeshIndex];
	if (sm.indexCount == 0) return;

	commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
	commandList->IASetIndexBuffer(&m_IndexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawIndexedInstanced(sm.indexCount, 1, sm.indexStart, 0, 0);
}
