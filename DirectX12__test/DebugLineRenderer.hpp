#pragma once

#include "Defines.hpp"
#include "RenderContext.hpp"

class DebugLineRenderer
{
public:
	/// @brief 初期化
	/// @param device デバイス 
	/// @param linePso 描画用のPSO
	void Init(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const ComPtr<ID3D12PipelineState>& linePso);

	void Begin();

	/// @brief ラインの追加
	/// @param start 追加位置
	/// @param end 終了位置
	/// @param color ラインの色
	void AddLine(
		_In_ const float3& start,
		_In_ const float3& end,
		_In_ const float4& color);

	/// @brief 描画
	/// @param context 描画に必要な情報 
	void Draw(
		_In_ const RenderContext& context);
private:
	/// @brief ライン用頂点データ
	struct LineVertex
	{
		float3 pos;
		float4 color;
	};

	/// @brief VS用定数バッファのデータ構造
	struct alignas(256) LineConstantBuffer
	{
		float4x4 viewProj;
	};

	static constexpr UINT MAX_LINES = 4096;
	static constexpr UINT MAX_VERTICES = MAX_LINES * 2;
	static constexpr UINT CB_SIZE = (sizeof(LineConstantBuffer) + 255u) & ~255u;

	std::vector<LineVertex> m_Vertices;
	ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView{};
	LineVertex* m_MappedVertexBuffer = nullptr;

	ComPtr<ID3D12Resource> m_ConstantBuffer;
	std::uint8_t* m_MappedConstants = nullptr;

	ComPtr<ID3D12PipelineState> m_LinePSO;
};

