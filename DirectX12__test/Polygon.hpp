/*****************************************************************//**
 * \file   Polygon.hpp
 * \brief  ポリゴン描画関係のクラス
 * 
 * 作成者 
 * 作成日 2026/2/19
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "DirectX.hpp"

class Polygon
{
public:
	static void Init(ComPtr<ID3D12Device> device);
	static void CreatePolygon();

	static void SetWorld(float4x4 world);
	static void SetView(float4x4 view);
	static void SetProjection(float4x4 proj);
	static void Draw(ID3D12GraphicsCommandList* cmdList);
private:
	Polygon(const Polygon&)				= delete;
	Polygon& operator=(const Polygon&)  = delete;
	Polygon()							= delete;
	~Polygon();

	static float4x4 m_wvp[3];

	static ComPtr<ID3D12Resource>		m_VertexBuffer;
	static D3D12_VERTEX_BUFFER_VIEW		m_VertexBufferView;
	static ComPtr<ID3D12Resource>		m_IndexBuffer;
	static D3D12_INDEX_BUFFER_VIEW		m_IndexBufferView;
	static ComPtr<ID3D12Resource>		m_ConstantBuffer;
	static ComPtr<ID3D12Device>			m_Device;
};

