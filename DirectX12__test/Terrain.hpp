/*****************************************************************//**
 * \file   Terrain.hpp
 * \brief  地形メッシュの生成(はいとマップ -> 格子メッシュ)
 * 
 * 作成者 keeeep
 * 作成日 2026/9/18
 * 更新履歴 9.18 作成
 * *********************************************************************/
#pragma once

#include <string>

#include "World.hpp"
#include "Defines.hpp"

struct TerrainComponent;

namespace Terrain
{
	/// @brief 高さを読み直してメッシュを作る(設定が変わったとき)
	void Rebuild(_In_ World& world, _In_ Entity entity);

	/// @brief いまの heights からメッシュだけ作り直す(ブラシ編集中に使う)
	void BuildMesh(_In_ World& world, _In_ Entity entity);

	size_t HashSettings(_In_ const TerrainComponent& terrain);

	/// @brief 地形のローカル座標での地面の高さ
	float SampleHeight(_In_ const TerrainComponent& terrain, _In_ float x, _In_ float z);

	/// @brief 彫った高さを .r16(16bit 生データ)で書き出す
	/// @return 成功したら true
	bool SaveHeights(_In_ const TerrainComponent& terrain, _In_ const std::string& path);

	// ------------------------------------------------------------------ //
	//                          ブラシ(エディタ用)                        //
	// ------------------------------------------------------------------ //

	enum class BrushMode { Raise, Lower, Smooth, Flatten };

	struct Brush
	{
		BrushMode mode = BrushMode::Raise;
		float radius = 5.0f;		// ワールド単位
		float strength = 8.0f;		// 1秒あたりの高さの変化量
		float targetHeight = 0.0f;	// Flatten の目標(ワールド単位)
	};

	/// @brief 光線と地形の交点を求める(地形のローカル座標で返す)
	/// @note heights を直接見るので、物理もコライダーも要らない
	bool Raycast(
		_In_ const TerrainComponent& terrain,
		_In_ const float4x4& terrainWorld,
		_In_ const float3& rayOrigin,
		_In_ const float3& rayDir,
		_In_ float maxDistance,
		_Out_ float3& outLocalHit);

	/// @brief ブラシを1回ぶん当てる(heights を書き換える。メッシュは呼び出し側で作り直す)
	/// @param center 地形のローカル座標
	void ApplyBrush(
		_Inout_ TerrainComponent& terrain,
		_In_ const float3& center,
		_In_ const Brush& brush,
		_In_ float deltaTime);
}