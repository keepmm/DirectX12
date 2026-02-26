#include "Time.hpp"

#pragma comment(lib, "winmm.lib")

float Time::GetDeltaTime() const
{
	return m_DeltaTime;
}

float Time::GetTotalTime() const
{
	return m_TotalTime;
}

void Time::SetTimeScale(float scale)
{
	m_TimeScale = scale;
}

float Time::GetTimeScale() const
{
	return m_TimeScale;
}

float Time::GetScaleDeltaTime() const
{
	return m_DeltaTime * m_TimeScale;
}

int Time::GetFPS() const
{
	return m_FPS;
}

int Time::GetFrameCount() const
{
	return m_FrameCount;
}


Time::Time() :
	m_DeltaTime(0.0f),
	m_TotalTime(0.0f),
	m_TimeScale(1.0f),
	m_FPS(0),
	m_FrameCount(0),
	m_PrevTime(0),
	m_StartTime(0),
	m_FPSFrameCount(0),
	m_FPSLastTime(0)
{
}

Time* Time::GetInstance()
{
	static Time instance;
	return &instance;
}

void Time::Init()
{
	timeBeginPeriod(1); // タイマーの精度を1msに設定
	m_StartTime = timeGetTime();
	m_PrevTime = m_StartTime;
	m_FPSLastTime = m_StartTime;
	m_DeltaTime = 0.0f;
	m_TotalTime = 0.0f;
	m_TimeScale = 1.0f;
	m_FPS = 0;
	m_FrameCount = 0;
	m_FPSFrameCount = 0;
}

void Time::Update()
{
	// 現在時間の更新
	DWORD currentTime = timeGetTime();

	// デルタタイムの計算
	// 計算は(ミリ秒から秒に変換)
	m_DeltaTime = (currentTime - m_PrevTime) / 1000.0f;
	m_PrevTime = currentTime;

	// 装経過時間の更新
	m_TotalTime = (currentTime - m_StartTime) / 1000.0f;

	// フレームカウント
	m_FrameCount++;

	// FPSの計測
	m_FPSFrameCount++;
	DWORD fpsDiff = currentTime - m_FPSLastTime;
	if (fpsDiff >= 1000)
	{
		m_FPS = static_cast<int>(m_FPSFrameCount * 1000.0f / fpsDiff);
		m_FPSFrameCount = 0;
		m_FPSLastTime = currentTime;
	}
}
