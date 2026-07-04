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
	std::string  name;
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

	// 回転 / 移動継承
	int appendParentIndex = -1;	// 継承元のボーンインデックス(-1は継承なし)
	float appendRate = 0.0f;
	bool appendRotation = false;
	bool appendMove = false;
};

/**
 * .IKリンクの定義
 */
struct IkLink
{
	int boneIndex = -1;	// ボーンのインデックス(-1は無効)
	bool hasLimit = false;	// 回転制限があるか
	float3 lowerLimit{ 0.0f, 0.0f, 0.0f };	// 回転制限の下限（ラジアン）
	float3 upperLimit{ 0.0f, 0.0f, 0.0f };	// 回転制限の上限（ラジアン）
};

/**
 *	Ikに必要なデータの定義
 */
struct IkData
{
	int boneIndex	= -1;		// IKのボーンのインデックス(-1は無効)
	int targetIndex = -1;		// エフェクタ(足首)
	int loopCount = 0;			// IKの反復回数
	float limitAngle = 0.0f;	// 1反復当たりの制限角
	std::vector<IkLink> links;	// ひざ、足(エフェクタに近い順)
};

/*
*	スケルトンの定義
*/
struct Skeleton
{
	std::vector<BoneNode> nodes;						// ボーンノードのリスト
	std::unordered_map<std::string,int> nameToIndex;	// ボーン名からインデックスへのマップ
	std::vector<IkData> iks;							// IKデータのリスト
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

#pragma region アニメーションデータ

/**
 *	キーフレームの定義
 */
struct KeyFrame
{
	float time = 0.0f;	// きーふれむの時間(秒)
	POSITION position{0.0f, 0.0f, 0.0f};
	QUATERNION rotation{0.0f, 0.0f, 0.0f, 1.0f};
	SCALE scale{ 1.0f, 1.0f, 1.0f };
};

/**
 *	ボーンアニメーションチャンネルの定義
 */
struct BoneAnimationChannel
{
	std::string boneName;
	std::vector<KeyFrame> keyFrames;
};

struct AnimationClip
{
	std::string name;
	float duration = 0.0f;	// アニメーションの長さ(秒)
	float tickPerSecond = 0.0f;	// 1秒あたりのティック数
	std::vector<BoneAnimationChannel> channels;
};

#pragma endregion

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
	std::vector<AnimationClip> clips;				// アニメーションデータ
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
	std::vector<AnimationClip> 	clips;	
	Skeleton                    skeleton;
	SkinData                    skinData;

	bool                        success = false;
};
