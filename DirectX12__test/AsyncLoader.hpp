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
    AsyncLoader(
        _In_ ThreadPool& pool,
        _In_ const ComPtr<ID3D12Device>& device)
        : m_Pool(pool), m_Device(device) {
    }

    /// @brief モデルを非同期でロードする（即戻る）
    void LoadModelAsync(
        _In_ const std::string& filepath,
        float scale,
        std::function<void(ModelLoadResult)> onDone)
    {
        Pending p;
        // CPUパースだけを別スレッドへ（deviceは触らない）
        p.future = m_Pool.Enqueue([filepath, scale]() {
            return ModelLoader::ParseFile(filepath, scale);
			});
        p.onDone = std::move(onDone);
        m_Pending.push_back(std::move(p));
    }

    /// @brief 完了したタスクの結果を処理する
    void ProcessCompletedTasks()
    {
        for (auto it = m_Pending.begin(); it != m_Pending.end(); )
        {
            // 完了していないものは飛ばす（ノンブロッキング）
            if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++it;
                continue;
            }

            // CPUパース結果を取り出し、メインスレッドでGPU化
            ModelCpuData cpu = it->future.get();
            ModelLoadResult result = ModelLoader::upload(cpu);

            if (it->onDone) it->onDone(std::move(result));
            it = m_Pending.erase(it);
        }
    }

    /// @brief 処理待ちのタスク数（デバッグ/進捗表示用）
    size_t PendingCount() const { return m_Pending.size(); }

private:
    struct Pending
    {
        std::future<ModelCpuData> future;
        std::function<void(ModelLoadResult)> onDone;
    };
    ThreadPool& m_Pool;
    ComPtr<ID3D12Device> m_Device;
    std::vector<Pending> m_Pending;
};