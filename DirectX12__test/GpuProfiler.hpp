/*****************************************************************//**
 * \file   GpuProfiler.hpp
 * \brief  タイムスタンプクエリでGPUの実行時間をパスごとに計測する
 *
 * 作成者 keepmm
 * 作成日 2026/9/9
 * 更新履歴
 *   2026/9/9 新規作成
 *
 * 使い方: 計りたい描画パスの先頭に GPU_PROFILE_SCOPE(cmd, "名前") を置く。
 *   { GPU_PROFILE_SCOPE(cmd, "Draw/Shadow"); m_ShadowSystem.Draw(...); }
 * 結果は Profiler と同じ「プロファイラ」ウィンドウに "GPU/名前" で並ぶ。
 *
 * 仕組み: パスの前後で EndQuery を撃ち、フレーム末尾で ResolveQueryData により
 * リードバックバッファへ吸い出す。GPUは非同期なので値が取れるのは RTV_NUM フレーム後。
 * スロットのフェンス待ちが済んだ直後(BeginFrame)に前回ぶんを回収している。
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "Profiler.hpp"

#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>

class GpuProfiler
{
public:
	static constexpr UINT MAX_MARKERS = 64;	// 1フレームに置けるスコープ数
	static constexpr UINT QUERY_PER_SLOT = MAX_MARKERS * 2;

	static GpuProfiler& Get()
	{
		static GpuProfiler instance;
		return instance;
	}

	/// @brief デバイスとキューが出来た直後に一度だけ呼ぶ
	bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue)
	{
		if (m_Ready || device == nullptr || queue == nullptr) return m_Ready;

		// GPUタイマの周波数。COPY キューなどでは 0 が返ることがあるので確認する
		if (FAILED(queue->GetTimestampFrequency(&m_Frequency)) || m_Frequency == 0)
		{
			return false;
		}

		D3D12_QUERY_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		heapDesc.Count = QUERY_PER_SLOT * RTV_NUM;
		heapDesc.NodeMask = 0;
		if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_QueryHeap))))
		{
			return false;
		}

		// 読み出し先。リードバックヒープは常に COPY_DEST なので遷移は要らない
		D3D12_HEAP_PROPERTIES hp{};
		hp.Type = D3D12_HEAP_TYPE_READBACK;

		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		rd.Width = sizeof(UINT64) * QUERY_PER_SLOT * RTV_NUM;
		rd.Height = 1;
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_UNKNOWN;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if (FAILED(device->CreateCommittedResource(
			&hp, D3D12_HEAP_FLAG_NONE, &rd,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
			IID_PPV_ARGS(&m_Readback))))
		{
			return false;
		}

		// 毎フレーム Map/Unmap すると重いので張りっぱなしにする
		D3D12_RANGE readAll{ 0, static_cast<SIZE_T>(rd.Width) };
		if (FAILED(m_Readback->Map(0, &readAll, reinterpret_cast<void**>(&m_Mapped))))
		{
			return false;
		}

		m_Ready = true;
		return true;
	}

	void Shutdown()
	{
		if (m_Readback && m_Mapped)
		{
			m_Readback->Unmap(0, nullptr);
			m_Mapped = nullptr;
		}
		m_Readback.Reset();
		m_QueryHeap.Reset();
		m_Ready = false;
	}

	/// @brief フレーム記録の先頭で呼ぶ。前回このスロットで撃った結果を回収する
	/// @note 呼び出し側でスロットのフェンス待ちが済んでいること
	void BeginFrame(ID3D12GraphicsCommandList* cmd, UINT slot)
	{
		if (!m_Ready || cmd == nullptr || slot >= static_cast<UINT>(RTV_NUM)) return;

		Collect(slot);

		m_CurrentSlot = slot;
		m_Slots[slot].count = 0;
		m_Slots[slot].pending = false;
		m_Recording = true;

		// フレーム全体。これが FrameMs に近ければGPU律速
		m_FrameMarker = Begin(cmd, "Frame total");
	}

	/// @brief フレーム記録の末尾、Close() の直前で呼ぶ
	void EndFrame(ID3D12GraphicsCommandList* cmd)
	{
		if (!m_Ready || !m_Recording || cmd == nullptr) return;

		End(cmd, m_FrameMarker);
		m_FrameMarker = -1;

		Slot& s = m_Slots[m_CurrentSlot];
		if (s.count > 0)
		{
			cmd->ResolveQueryData(
				m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
				m_CurrentSlot * QUERY_PER_SLOT, s.count * 2,
				m_Readback.Get(),
				sizeof(UINT64) * m_CurrentSlot * QUERY_PER_SLOT);
			s.pending = true;
		}
		m_Recording = false;
	}

	/// @brief 区間の開始。戻り値を End へ渡す(溢れたら -1)
	int Begin(ID3D12GraphicsCommandList* cmd, const char* name)
	{
		if (!m_Ready || !m_Recording || cmd == nullptr) return -1;

		Slot& s = m_Slots[m_CurrentSlot];
		if (s.count >= MAX_MARKERS) return -1;

		const int index = static_cast<int>(s.count++);
		s.names[index] = Intern(name);
		cmd->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
			m_CurrentSlot * QUERY_PER_SLOT + index * 2);
		return index;
	}

	void End(ID3D12GraphicsCommandList* cmd, int index)
	{
		if (!m_Ready || !m_Recording || cmd == nullptr || index < 0) return;

		cmd->EndQuery(m_QueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
			m_CurrentSlot * QUERY_PER_SLOT + index * 2 + 1);
	}

	bool IsReady() const noexcept { return m_Ready; }

private:
	struct Slot
	{
		UINT count = 0;
		bool pending = false;
		const char* names[MAX_MARKERS] = {};
	};

	/// @brief このスロットの前回ぶんを Profiler へ流し込む
	void Collect(UINT slot)
	{
		Slot& s = m_Slots[slot];
		if (!s.pending || m_Mapped == nullptr) return;
		s.pending = false;

		const UINT64* src = m_Mapped + slot * QUERY_PER_SLOT;
		for (UINT i = 0; i < s.count; ++i)
		{
			const UINT64 t0 = src[i * 2];
			const UINT64 t1 = src[i * 2 + 1];

			// End を呼び忘れたスコープや、投げたが実行されなかったパスを弾く
			if (t1 <= t0) continue;

			const double ms = static_cast<double>(t1 - t0) * 1000.0
				/ static_cast<double>(m_Frequency);
			Profiler::Get().Add(s.names[i], ms);
		}
	}

	/// @brief "GPU/名前" を寿命の切れないポインタにして返す
	/// @note Profiler は const char* をそのまま保持するので、一時文字列は渡せない
	const char* Intern(const char* name)
	{
		std::string key = "GPU/";
		key += (name ? name : "?");

		for (const auto& p : m_Pool)
		{
			if (*p == key) return p->c_str();
		}
		m_Pool.push_back(std::make_unique<std::string>(std::move(key)));
		return m_Pool.back()->c_str();
	}

	ComPtr<ID3D12QueryHeap> m_QueryHeap;
	ComPtr<ID3D12Resource>  m_Readback;
	UINT64* m_Mapped = nullptr;
	UINT64  m_Frequency = 0;

	Slot m_Slots[RTV_NUM]{};
	UINT m_CurrentSlot = 0;
	int  m_FrameMarker = -1;
	bool m_Ready = false;
	bool m_Recording = false;

	std::vector<std::unique_ptr<std::string>> m_Pool;
};

/// @brief スコープを抜けるときに終端タイムスタンプを撃つ
class GpuProfileScope
{
public:
	GpuProfileScope(ID3D12GraphicsCommandList* cmd, const char* name)
		: m_Cmd(cmd), m_Index(GpuProfiler::Get().Begin(cmd, name))
	{
	}
	~GpuProfileScope()
	{
		GpuProfiler::Get().End(m_Cmd, m_Index);
	}

	GpuProfileScope(const GpuProfileScope&) = delete;
	GpuProfileScope& operator=(const GpuProfileScope&) = delete;

private:
	ID3D12GraphicsCommandList* m_Cmd;
	int m_Index;
};

#define GPU_PROFILE_SCOPE(cmd, name) \
	GpuProfileScope PROFILE_CONCAT(_gpuProfScope, __LINE__)((cmd), (name))
