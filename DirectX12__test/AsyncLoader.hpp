/*****************************************************************//**
 * \file   AsyncLoader.hpp
 * \brief  非同期処理のロード
 * 
 * 作成者 keeeep
 * 作成日 2026/6/28
 * 更新履歴	6.28 作成
 * *********************************************************************/
#pragma once

#include "Defines.hpp"
#include "Engine/ThreadPool.hpp"
#include "ModelData.hpp"
#include "ModelLoader.hpp"
#include <chrono>

class AsyncLoader
{
public:
	static AsyncLoader& Get()
	{
		static AsyncLoader s;
		return s;
	}

	// 遅延初期化（Deviceが用意できてから）
	void Init(_In_ ThreadPool* pool, _In_ const ComPtr<ID3D12Device>& device)
	{
		m_Pool = pool;
		m_Device = device;
	}

	bool IsReady() const { return m_Pool != nullptr; }

	// 終了時に明示的に片付ける（静的破棄順の事故防止）
	void Shutdown()
	{
		m_Pending.clear();
		m_Pool = nullptr;
		m_Device.Reset();
	}

	void LoadModelAsync(
		_In_ const std::string& filepath,
		float scale,
		std::function<void(ModelLoadResult)> onDone)
	{
		if (m_Pool == nullptr) return;   // 未初期化ガード

		Pending p;
		p.future = m_Pool->Enqueue([filepath, scale]() {
			return ModelLoader::ParseFile(filepath, scale);
			});
		p.onDone = std::move(onDone);
		m_Pending.push_back(std::move(p));
	}

	void LoadVMDAsync(
		_In_ const std::string& filePath,
		_In_ const Skeleton& skeleton,
		std::function<void(AnimationClip)> onDone
	)
	{
		if (m_Pool == nullptr) return;

		PendingClip p;
		Skeleton skelCopy = skeleton; // スケルトンをコピーしてラムダに渡す
		p.future = m_Pool->Enqueue([filePath, skelCopy]() {
			return ModelLoader::LoadVMDClip(filePath, skelCopy);
			});
		p.onDone = std::move(onDone);
		m_PendingClips.push_back(std::move(p));
	}

	void ProcessCompletedTasks()
	{
		for (auto it = m_Pending.begin(); it != m_Pending.end(); )
		{
			if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				++it;
				continue;
			}
			ModelCpuData cpu = it->future.get();
			ModelLoadResult result = ModelLoader::upload(std::move(cpu));
			if (it->onDone) it->onDone(std::move(result));
			it = m_Pending.erase(it);
		}

		for (auto it = m_PendingClips.begin(); it != m_PendingClips.end(); )
		{
			if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				++it;
				continue;
			}
			AnimationClip clip = it->future.get();
			if (it->onDone) it->onDone(std::move(clip));   // メインスレッドで実行
			it = m_PendingClips.erase(it);
		}
	}

	size_t PendingCount() const { return m_Pending.size(); }

private:
	AsyncLoader() = default;                       // シングルトン
	AsyncLoader(const AsyncLoader&) = delete;
	AsyncLoader& operator=(const AsyncLoader&) = delete;

	struct Pending
	{
		std::future<ModelCpuData> future;
		std::function<void(ModelLoadResult)> onDone;
	};

	struct PendingClip
	{
		std::future<AnimationClip> future;
		std::function<void(AnimationClip)> onDone;
	};
	std::vector<PendingClip> m_PendingClips;

	ThreadPool* m_Pool = nullptr;                  // 参照→ポインタ（後から差せる）
	ComPtr<ID3D12Device> m_Device;
	std::vector<Pending> m_Pending;
};