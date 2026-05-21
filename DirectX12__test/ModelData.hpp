/*****************************************************************//**
 * \file   ModelData.hpp
 * \brief  モデルデータの定義
 * 
 * 作成者 
 * 作成日 2026/5/4
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "Mesh.hpp"

/*
*	ボーンノードの定義
*/
struct BoneNode
{
	std::string name;			// ボーンの名前
	int parentIndex = -1;		// 親ボーンのインデックス（-1はルートボーン）
	float4x4 localTransform{};	// ローカル変換行列
	std::vector<int> children;	// 子ボーンのインデックス
};

/*
*	スケルトンの定義
*/
struct Skeleton
{
	std::vector<BoneNode> nodes;						// ボーンノードのリスト
	std::unordered_map<std::string,int> nameToIndex;	// ボーン名からインデックスへのマップ
};

/*
 *	ボーンの影響データの定義
 */
struct BoneInfuence
{
	std::array<std::uint16_t, 4> indices{};	// ボーンのインデックス
	std::array<float, 4> weights{};			// ボーンの影響度	
};

/**
 *  スキニングデータの定義
 */
struct SkinData
{
	std::vector<BoneInfuence> infuences;						// 頂点ごとのボーンの影響データ
	std::vector<std::string> boneNames;								// ボーンの名前
	std::vector<float4x4> offsetMatrices;						// ボーンのオフセット行列
	std::unordered_map<std::string, std::uint16_t> boneNametoIndex;	// ボーン名からインデックスへのマップ
};

/**
 *	モデルのロード結果の定義
 */
struct ModelLoadResult
{
	std::shared_ptr<Mesh> mesh;						// メッシュデータ
	std::wstring diffuseTexturePath;				//テクスチャのファイルパス
	std::vector<std::uint8_t> diffusetextureData;	// 埋め込みテクスチャ
	Skeleton skeleton;								// スケルトンデータ
	SkinData skinData;								// スキニングデータ
};
