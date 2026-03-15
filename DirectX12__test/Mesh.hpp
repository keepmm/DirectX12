#pragma once

#include "DirectX.hpp"
#include "Vertex.hpp"
#include <vector>
#include <cstdint>

/// @brief メッシュの一部を表すサブメッシュ構造体
struct SubMesh
{
	UINT indexStart		= 0; // インデックスバッファ内の開始位置
	UINT indexCount		= 0; // インデックスの数
	UINT materialIndex	= 0;
};

class Mesh
{
public:
	void Init(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const std::vector<Vertex>& vertices,
		_In_ const std::vector<std::uint32_t>& indices,
		_In_opt_ const std::vector<SubMesh>* subMeshes = nullptr);

	void CreateCube(_In_ const ComPtr<ID3D12Device>& device);
	void Draw(_In_ ID3D12GraphicsCommandList* commandList) const;

	void DrawSubMesh(_In_ ID3D12GraphicsCommandList* commandList, UINT subMeshIndex) const;

	/// @brief サブメッシュの数を取得
	/// @return サブメッシュの数
	UINT GetSubMeshCount() const { return static_cast<UINT>(m_SubMeshes.size()); }
private:
	ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView{};

	ComPtr<ID3D12Resource> m_IndexBuffer;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView{};

	UINT m_IndexCount = 0;
	std::vector<SubMesh> m_SubMeshes;
};

