/*****************************************************************//**
 * \file   DebugWindow.hpp
 * \brief  imguiを用いたデバッグウィンドウ
 * 
 * 作成者 keeeep
 * 作成日 2026/4/27
 * 更新履歴 4.27 作成
 * *********************************************************************/
#pragma once

class DebugWindow
{
public:
	void Draw();

private:
	void DrawPeformanceTab() const;
	void DrawMemoryTab();
	void DrawLogTab() const;

private:
	static constexpr int MEMORY_HISTORY_SIZE = 240; // 1時間分のメモリ使用量を記録 (1秒ごとに記録)

	std::array<float, MEMORY_HISTORY_SIZE> m_PrivateWorkingSetHistory = {};
	std::array<float, MEMORY_HISTORY_SIZE> m_CommitHistoryMB = {};
	int m_HistoryIndex = 0;
	bool m_HistoryFilled = false;

	float m_MemoryWarningThresholdMB = 500.0f; // メモリ使用量の警告閾値 (MB)
	bool m_EnableMemoryWarning = true;
	bool m_MemoryWarningSent = false;

	bool m_LogAutoScroll = true;
	char m_LogFilter[256] = {};
};

