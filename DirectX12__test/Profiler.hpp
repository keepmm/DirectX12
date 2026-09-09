/*****************************************************************//**
 * \file   Profiler.hpp
 * \brief  区間ごとのCPU処理時間を計測して一覧表示するための軽量プロファイラ
 *
 * 作成者 keepmm
 * 作成日 2026/9/9
 * 更新履歴
 *   2026/9/9 新規作成
 *
 * 使い方: 計りたいスコープの先頭に PROFILE_SCOPE("名前") を置くだけ。
 *   void Foo() { PROFILE_SCOPE("Foo"); ... }
 * 結果はエディタの「プロファイラ」ウィンドウに出る。
 *
 * 注意: これはCPU時間(コマンド積み込みまで)しか測れない。
 * GPUの実行時間は非同期なので、描画パスの数値は「CPUがどれだけ掛かったか」であって
 * 「GPUがどれだけ掛かったか」ではない。GPU側はタイムスタンプクエリが別途必要。
 * *********************************************************************/
#pragma once

#include <chrono>
#include <cstring>
#include <vector>

class Profiler
{
public:
	struct Entry
	{
		const char* name = nullptr;
		double ms = 0.0;		// このフレームの合計
		double avg = 0.0;		// 指数移動平均(表示のちらつき防止)
		int    calls = 0;		// このフレームの呼ばれた回数
	};

	static Profiler& Get()
	{
		static Profiler instance;
		return instance;
	}

	/// @brief フレーム先頭で呼ぶ。前フレームの値を平均へ畳んでリセットする
	void BeginFrame()
	{
		using clk = std::chrono::high_resolution_clock;
		const auto now = clk::now();

		if (m_HasPrev)
		{
			m_FrameMs = std::chrono::duration<double, std::milli>(now - m_PrevFrame).count();
			m_FrameAvg = (m_FrameAvg <= 0.0) ? m_FrameMs : m_FrameAvg * 0.9 + m_FrameMs * 0.1;
		}
		m_PrevFrame = now;
		m_HasPrev = true;

		for (auto& e : m_Entries)
		{
			e.avg = (e.avg <= 0.0) ? e.ms : e.avg * 0.9 + e.ms * 0.1;
			e.ms = 0.0;
			e.calls = 0;
		}
	}

	void Add(const char* name, double ms)
	{
		if (!m_Enabled || name == nullptr) return;

		for (auto& e : m_Entries)
		{
			// 文字列リテラル前提なのでポインタ比較で当たることが多い
			if (e.name == name || std::strcmp(e.name, name) == 0)
			{
				e.ms += ms;
				e.calls++;
				return;
			}
		}
		m_Entries.push_back(Entry{ name, ms, ms, 1 });
	}

	const std::vector<Entry>& Entries() const noexcept { return m_Entries; }
	double FrameMs()  const noexcept { return m_FrameMs; }
	double FrameAvg() const noexcept { return m_FrameAvg; }

	bool  IsEnabled() const noexcept { return m_Enabled; }
	void  SetEnabled(bool v) noexcept { m_Enabled = v; }
	void  Clear() { m_Entries.clear(); }

private:
	std::vector<Entry> m_Entries;
	std::chrono::high_resolution_clock::time_point m_PrevFrame{};
	bool   m_HasPrev = false;
	bool   m_Enabled = true;
	double m_FrameMs = 0.0;
	double m_FrameAvg = 0.0;
};

/// @brief スコープを抜けるときに経過時間を Profiler へ積む
class ProfileScope
{
public:
	explicit ProfileScope(const char* name)
		: m_Name(name), m_Start(std::chrono::high_resolution_clock::now())
	{
	}
	~ProfileScope()
	{
		const auto end = std::chrono::high_resolution_clock::now();
		Profiler::Get().Add(m_Name,
			std::chrono::duration<double, std::milli>(end - m_Start).count());
	}

	ProfileScope(const ProfileScope&) = delete;
	ProfileScope& operator=(const ProfileScope&) = delete;

private:
	const char* m_Name;
	std::chrono::high_resolution_clock::time_point m_Start;
};

#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)
#define PROFILE_SCOPE(name) ProfileScope PROFILE_CONCAT(_profScope, __LINE__)(name)
