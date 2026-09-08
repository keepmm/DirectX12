/*****************************************************************//**
 * \file   LiveTimeline.hpp
 * \brief  楽曲時間で照明・カメラを駆動するライブ演出キュー / トラック
 *
 * 作成者 keepmm
 * 作成日 2026/9/4
 * 更新履歴
 *   2026/9/4 新規作成
 *   2026/9/4 キーフレーム補間トラック(LiveTrack)を追加
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include <string>
#include <vector>

// 瞬間的に発火する演出(カット切替・消灯・花火など)
struct LiveCue
{
	enum class Type : uint8_t
	{
		CameraCut,		// カメラ切り替え
		LightColor,		// ライト色変更
		LightIntensity,	// ライト強度
		SwingEnable,	// 首振りON/OFF
		Blackout,		// 全消灯
		Firework,		// 花火/爆発トリガ
	} type = Type::LightColor;

	float time = 0.0f;	// 発火する曲位置(秒)
	float fade = 0.0f;	// 補間時間(秒、0なら瞬時)
	std::string target;	// 対象Entity名(空なら全体)
	COLOR color{ 1.0f,1.0f,1.0f,1.0f };
	float value = 1.0f;	// 強度などの汎用パラメータ

	bool fired = false;	// ランタイム状態(シリアライズ対象外)
};

// 曲位置で連続的に補間する値(位置・向き・色・強度など)
struct LiveKey
{
	float time = 0.0f;
	float4 value{ 0.0f, 0.0f, 0.0f, 0.0f };	// 使う成分はプロパティ依存
};

struct LiveTrack
{
	enum class Property : uint8_t
	{
		Position,		// TransformComponent.position (xyz)
		EulerAngles,	// TransformComponent.EulerAngles (xyz、度)
		Color,			// LightComponent.color (rgba)
		Intensity,		// LightComponent.intensity (x)
		Range,			// LightComponent.range (x)
		SpotAngle,		// LightComponent.spotAngle (x)
	} property = Property::Position;

	std::string target;			// 対象Entity名(必須)
	bool enabled = true;
	std::vector<LiveKey> keys;	// 時刻昇順

	void SortByTime();

	/// @brief 指定時刻の値を線形補間で求める(キーが空なら false)
	bool Evaluate(float time, float4& out) const;

	/// @brief 同じ時刻のキーがあれば上書き、なければ挿入する
	void SetKey(float time, const float4& value);

	/// @brief 指定時刻に最も近いキーの添字を返す(見つからなければ -1)
	int FindKey(float time, float tolerance = 1.0f / 60.0f) const;
};

class LiveTimeline
{
public:
	std::vector<LiveCue>   cues;
	std::vector<LiveTrack> tracks;

	/// @brief 時刻の昇順に並べ替える(cues / tracks 両方)
	void SortByTime();

	/// @brief シーク時に fired を張り直す(fromTime より前は発火済み扱い)
	void ResetFired(float fromTime);

	/// @brief 一番遅いキー/キューの時刻(タイムライン表示の長さに使う)
	float Duration() const;

	bool LoadJson(const std::string& path);
	bool SaveJson(const std::string& path) const;
};
