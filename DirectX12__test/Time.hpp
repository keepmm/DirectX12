/*****************************************************************//**
 * \file   Time.hpp
 * \brief  時間管理システム
 * 
 * 作成者 keepmm
 * 作成日 2026/2/26
 * 更新履歴 2026/2/26: 新規作成
 * *********************************************************************/
#pragma once
#include <Windows.h>

#define TIME Time::GetInstance()

class Time
{
public:
	static Time* GetInstance();

	/// @brief 初期化
	void Init();

	/// @brief 更新処理
	void Update();

	/// @brief デルタタイムの取得
	/// @return (前フレームからの経過時間 - 秒単位)
	float GetDeltaTime() const;

	/// @brief ゲーム開始からの総経過時間(秒単位)
	/// @return 装経過時間
	float GetTotalTime() const;

	/// @brief タイムスケール(スローモーション/早送り)の取得
	/// @brief 0.0f ~ 1.0f
	/// @param scale 設定するスケール(0.0f ~ 1.0f)
	void SetTimeScale(float scale);

	/// @brief 現在のタイムスケールを取得する
	/// @return 現在のタイムスケール(0.0f ~ 1.0f)
	float GetTimeScale() const;

	/// @brief スケール運用後のデルタタイム
	/// @return デルタタイム
	float GetScaleDeltaTime() const;

	/// @brief 現在のFPS
	/// @return 現在のFPS
	int GetFPS() const;

	/// @brief フレームカウントの取得
	/// @return フレームカウント
	int GetFrameCount() const;

private:
	Time();
	~Time() = default;
	Time(const Time&) = delete;
	void operator=(const Time&) = delete;

	float m_DeltaTime;	// デルタタイム
	float m_TotalTime;	// ゲームの装経過時間
	float m_TimeScale;	// タイムスケール(0.0f ~ 1.0f)
	int m_FPS;			// FPS
	int m_FrameCount;	// フレームカウント

	DWORD m_PrevTime;	// 前回の経過時間
	DWORD m_StartTime;	// スタート時間

	// FPS計測用
	int m_FPSFrameCount;
	DWORD m_FPSLastTime;
};

