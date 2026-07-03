/*****************************************************************//**
 * \file   ModelData.hpp
 * \brief  モデルデータの定義
 * 
 * 作成者 
 * 作成日 2026/5/4
 * 更新履歴	5.4 作成
 *			7.3 法線マップ、メタルマップ、ラフネスマップの追加
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "Mesh.hpp"

/**
 *.	
 */
struct DecodedImage
{
	std::vector<std::uint8_t> pixels;
	UINT width = 0;
	UINT height = 0;
	bool ok = false;
};

 /*
  *	1マテリアル分のテクスチャパス
  */
struct MaterialTextureSet
{
	std::wstring diffuse;
	std::wstring normal;
	std::wstring metal;
	std::wstring rough;

	DecodedImage diffuseImage;
	DecodedImage normalImage;
	DecodedImage metalImage;
	DecodedImage roughImage;
};

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
	std::vector<BoneInfuence> infuences;							// 頂点ごとのボーンの影響データ
	std::vector<std::string> boneNames;								// ボーンの名前
	std::vector<float4x4> offsetMatrices;							// ボーンのオフセット行列
	std::unordered_map<std::string, std::uint16_t> boneNametoIndex;	// ボーン名からインデックスへのマップ
};

/**
 *	モデルのロード結果の定義
 */
struct ModelLoadResult
{
	std::shared_ptr<Mesh> mesh;						// メッシュデータ

	std::vector<SubMesh> subMeshes;
	std::vector<MaterialTextureSet> materials;		// マテリアルごとのテクスチャパス

	std::wstring diffuseTexturePath;				//テクスチャのファイルパス
	std::wstring normalTexturePath;					// 法線マップのファイルパス
	std::wstring metalTexturePath;					// メタルマップのファイルパス
	std::wstring roughTexturePath;					// ラフネスマップのファイルパス
	std::vector<std::uint8_t> diffusetextureData;	// 埋め込みテクスチャ
	Skeleton skeleton;								// スケルトンデータ
	SkinData skinData;								// スキニングデータ
};

/*
 *	CPU側だけで保持するモデルデータの定義
 */
struct ModelCpuData
{
	std::vector<Vertex>         vertices;
	std::vector<std::uint32_t>  indices;

	std::vector<SubMesh> subMeshes;
	std::vector<MaterialTextureSet> materials;		// マテリアルごとのテクスチャパス

	std::wstring                diffuseTexturePath;
	std::wstring				normalTexturePath;	// 法線マップのファイルパス
	std::wstring				metalTexturePath;	// メタルマップのファイルパス
	std::wstring				roughTexturePath;	// ラフネスマップのファイルパス
	std::vector<std::uint8_t>   diffuseTextureData;
	Skeleton                    skeleton;
	SkinData                    skinData;
	bool                        success = false;
};
