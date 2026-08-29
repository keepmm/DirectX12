#pragma once

#include "Defines.hpp"
#include "RenderContext.hpp"

class BeamRenderer
{
public:
	/// @brief 初期化
	/// @param device デバイス
	/// @param beamPso 描画用のPSO（トライアングルリスト・加算合成）
	void Init(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const ComPtr<ID3D12PipelineState>& beamPso);

	void Begin();

	/// @brief 三角形の追加（頂点カラー・アルファはコーン/ビームのフェード用）
	void AddTriangle(
		_In_ const float3& a,
		_In_ const float3& b,
		_In_ const float3& c,
		_In_ const float4& color);

	/// @brief 頂点ごとに色を指定して三角形を追加(グラデーション用)
	void AddTriangle(
		_In_ const float3& a, _In_ const float4& colA,
		_In_ const float3& b, _In_ const float4& colB,
		_In_ const float3& c, _In_ const float4& colC);

	/// @brief 描画
	/// @param context 描画に必要な情報
	void Draw(
		_In_ const RenderContext& context, ID3D12PipelineState* psoOverride = nullptr);

private:
	struct BeamVertex
	{
		float3 pos;
		float4 color;
	};

	static constexpr UINT SLOT_NUM = RTV_NUM * 2;
	UINT m_DrawCursor = 0;

	struct alignas(256) BeamConstantBuffer
	{
		float4x4 viewProj;
	};

	static constexpr UINT MAX_TRIANGLES = 32768;
	static constexpr UINT MAX_VERTICES = MAX_TRIANGLES * 3;
	static constexpr UINT CB_SIZE = (sizeof(BeamConstantBuffer) + 255u) & ~255u;

	std::vector<BeamVertex> m_Vertices;
	ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView{};
	BeamVertex* m_MappedVertexBuffer = nullptr;

	ComPtr<ID3D12Resource> m_ConstantBuffer;
	std::uint8_t* m_MappedConstants = nullptr;

	ComPtr<ID3D12PipelineState> m_BeamPSO;
};