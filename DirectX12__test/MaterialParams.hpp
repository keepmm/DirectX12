/*! ************************************************************
 * \file   MaterialParams.hpp
 * \brief  マテリアルの見た目パラメータ(ただのデータ)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 Material から分離して作成
 *
 * \note D3D12 リソースを一切持たない。シリアライズ(SceneSerializer)と
 *       インスペクタ(InspectorWindow)はこの型だけ見れば済む
 * *********************************************************************/
#pragma once

#include <string>

#include "Defines.hpp"

struct MaterialCB;

struct MaterialParams
{
	float roughness = 0.5f;
	float metallic = 0.0f;
	float4 rimColor = { 1.0f,1.0f,1.0f,1.0f };

	// 平面反射(ステージ床など)。0 なら反射テクスチャを参照しない
	float reflectStrength = 0.0f;
	float reflectFade = 8.0f;	// この距離で反射が消える
	float reflectBlur = 1.0f;	// サンプルのぼかし半径(ピクセル)
	bool isFace = false;
	float outlineWidth = 1.0f;
	float sssStrength = 0.0f;	// 肌 : 0.5 ~ 0.7
	float sssWrap = 0.4f;		// 明暗境界のなだらかさ
	float sssTrans = 0.0f;		// 耳・指の逆光透過
	float sheen = 0.0f;			// 布 : 0.5 ~ 1.0
	COLOR sssColor = { 0.9f,0.35f,0.25f,1.0f };
	float baseAlpha = 1.0f;
	COLOR baseColor = { 1.0f,1.0f,1.0f,1.0f };
	COLOR emissiveColor = { 0.0f,0.0f,0.0f,1.0f };	// glTF の emissiveFactor
	float emissiveStrength = 1.0f;					// 演出用の倍率

	// 水面(Water シェーダー用)
	float waveHeight = 0.35f;	// 法線の起伏。大きいほど荒れる
	float waveLength = 2.0f;	// 一番長い波の波長(m)。池なら 1~2、海なら 10 台
	float waveSpeed = 0.6f;		// 流れる速さ。0 で静止
	float waterGloss = 1.0f;	// 鏡面の鋭さ。大きいほど点状のきらめき
	float waterOpacity = 0.55f;	// 真上から見たときの不透明度
	float waterTurbidity = 1.0f;// 濁り。大きいほど向こうが見えない
	float waterFoam = 0.0f;		// 白波のしきい値。0 で白波なし
	float waveAmplitude = 0.15f;// 頂点をどれだけ持ち上げるか(m)
	float waveSteepness = 0.5f;	// 波頭の尖り。1 に近づけると崩れる寸前の形

	std::string shaderName;

	/// @brief パラメータ由来のフィールドを定数バッファへ書き込む
	/// @note テクスチャの有無に由来するフィールド(mapFlags / pbrParams.xy /
	///       faceParam.z)は MaterialTextures::FillCB が埋める
	void FillCB(_Out_ MaterialCB& out) const;
};
