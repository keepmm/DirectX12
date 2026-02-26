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
	static void Init(
		ComPtr<ID3D12Device> device, 
		ComPtr<ID3D12GraphicsCommandList> commandList,
		ComPtr<ID3D12PipelineState> pipelineState,
		ComPtr<ID3D12PipelineState> pipelineStateWireFrame);
	static void CreatePolygon();

	static void SetWorld(float4x4 world);
	static void SetView(float4x4 view);
	static void SetProjection(float4x4 proj);

	static void DrawWireFrame(bool b);
	static void DrawPolygon(bool b);


	/* ライトの方向の設定 */
	static void SetLightDir(float3 dir) { m_LightDir = dir; }

	/* 光の色の設定 */
	static void SetLightColor(float4 color) { m_LightColor = color; }

	/* 環境光の設定 */
	static void SetAmbientColor(float4 color) { m_AmbientColor = color; }

	/* 描画 */
	static void Draw();
private:
	Polygon(const Polygon&)				= delete;
	Polygon& operator=(const Polygon&)  = delete;
	Polygon()							= delete;
	~Polygon();

	static bool m_IsDrawWireFrame;
	static bool m_isDrawPolygon;

	static float3 m_LightDir;
	static float4 m_LightColor;
	static float4 m_AmbientColor;

	static float4x4 m_wvp[3];

	static ComPtr<ID3D12Resource>				m_VertexBuffer;
	static D3D12_VERTEX_BUFFER_VIEW				m_VertexBufferView;
	static ComPtr<ID3D12Resource>				m_IndexBuffer;
	static D3D12_INDEX_BUFFER_VIEW				m_IndexBufferView;
	static ComPtr<ID3D12Resource>				m_ConstantBuffer;
	static ComPtr<ID3D12Device>					m_Device;
	static ComPtr<ID3D12GraphicsCommandList>	m_CommandList;
	static ComPtr<ID3D12PipelineState>			m_PipelineState;
	static ComPtr<ID3D12PipelineState>			m_PipelineStateWireFrame;
};

