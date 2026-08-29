/*****************************************************************//**
 * \file   ModelData.hpp
 * \brief  モデルデータの定義
 * 
 * 作成者 keeep
 * 作成日 2026/5/4
 * 更新履歴	5.4 作成
 *			7.3 法線マップ、メタルマップ、ラフネスマップの追加
 *			7.4 スケルトン、スキニングデータ、アニメーションデータの追加
 *			7.5 Morphデータ、物理データの追加
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

	std::shared_ptr<DecodedImage> diffuseImage;
	std::shared_ptr<DecodedImage> normalImage;
	std::shared_ptr<DecodedImage> metalImage;
	std::shared_ptr<DecodedImage> roughImage;

	COLOR diffuseColor{ 1.0f,1.0f,1.0f,1.0f };
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

#pragma region Morph

/**
 * .頂点もーふの1オフセットの定義
 */
struct VertexMorphOffset
{
	std::uint32_t vertexIndex = 0;
	float3 offset{ 0.0f,0.0f,0.0f };
};

/**
 * .もーフ1つ分のデータ
 */
struct MorphData
{
	std::string name;
	int panel = 0;	// 0: 表示なし, 1: 顔, 2: 体, 3: その他
	int type = 1;	// 1: 頂点, 2: ボーン, 3: UV, 4: 材質
	std::vector<VertexMorphOffset> vertexOffsets;
	std::vector<std::pair<int, float>> groupOffsets;
};

/**
 * .MorphSet
 */
struct MorphSet
{
	std::vector<MorphData> morphs;
	std::unordered_map<std::string, int> nameToIndex;
};

#pragma endregion

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

struct CameraKeyFrame
{
	float time;			// 秒(frame/30)
	float distance;		// 注意視点からの距離(前方にあるとき負値)
	float3 target;		// 注視点
	float3 rotation;	// オイラー角(rad)
	float fovY;			// 度
};

struct CameraClip
{
	std::vector<CameraKeyFrame> keys;
	float duration = 0.0f;
};

#pragma endregion

#pragma region 剛体アニメーションデータ

/**
 *	PMX剛体.
 */
struct PmxRigidBody
{
	std::string name;
	int boneIndex = -1;						// 関連ボーン
	uint8_t group = 0;						// 衝突グループ
	uint16_t noCollisionMask = 0;			// 非衝突グループ
	uint8_t shape = 0;						// 0: 球, 1: 箱, 2: カプセル
	SCALE size{ 1.0f, 1.0f, 1.0f };			// 球:x=半径 箱:xyz半径 カプセル:x=半径,y=高さ
	POSITION position{ 0.0f, 0.0f, 0.0f };	// 剛体の位置(モデル準拠)
	float3 rotation{ 0.0f, 0.0f, 0.0f };	// 剛体の回転(モデル準拠、ラジアン)
	float mass				= 1.0f;			// 
	float linearDamping		= 0.0f;			//
	float angularDamping	= 0.0f;			//
	float restitution		= 0.0f;			//
	float friction			= 0.05f;		// 
	uint8_t physicsType = 0;				// 0: ボーン追従, 1: 物理演算, 2: 物理 + ボーン
};

/**
 * .PMXジョイント(6DOF)の定義
 */
struct PmxJoint
{
	std::string name;
	int rigidBodyA = -1;
	int rigidBodyB = -1;
	float3 position{ 0,0,0 };
	float3 rotation{ 0,0,0 };
	float3 moveLimitLower{ 0,0,0 };
	float3 moveLimitUpper{ 0,0,0 };
	float3 rotLimitLower{ 0,0,0 };
	float3 rotLimitUpper{ 0,0,0 };
	float3 springMove{ 0,0,0 };
	float3 springRot{ 0,0,0 };
};

/**
 * モデルの物理データ一式.
 */
struct PmxPhysics
{
	std::vector<PmxRigidBody> rigidBodies;
	std::vector<PmxJoint> joints;
};

#pragma endregion

#pragma region モデルロード

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
	MorphSet morphs;								// モーフデータ
	PmxPhysics					physics;
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

	MorphSet 					morphs;
	PmxPhysics					physics;

	bool                        success = false;
};

#pragma endregion
