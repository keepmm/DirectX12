/*****************************************************************//**
 * \file   Engine.hpp
 * \brief　エンジンに使用するクラスの宣言
 * 
 * 作成者 keep
 * 作成日 2026/5/15
 * 更新履歴	5.15 作成
 * *********************************************************************/
#pragma once

#include "../SceneManager.hpp"
#include "../DirectX.hpp"
#include "../FramePipeline.hpp"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

class Engine
{
public:
	virtual ~Engine() = default;

	HRESULT Init(
		_In_ HINSTANCE hInstance,
		_In_ int width,
		_In_ int height);
	void Run();
	void Terminate();
	virtual void ConfigureContext(RenderContext& renderContext) = 0;

	void SetGameMode(bool gameMode) { m_IsGameMode = gameMode; }
	bool IsGameMode() const { return m_IsGameMode; }

	// ---- サブクラスがオーバーライドする仮想関数 ---- //

	virtual HRESULT OnInit() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnShutDown() = 0;

protected:
	SceneManager m_SceneManager;
	std::unique_ptr<DirectXApp> m_DirectX;
	HWND m_hWnd = nullptr;
	HINSTANCE m_hInstance = nullptr;
	bool m_IsGameMode = false;

#ifdef _FRAMEPIPELINE
	FramePipeline m_FramePipeline[RTV_NUM];

	struct RenderJob
	{
		UINT64 frameNumber = 0;
	};

	std::thread				m_RenderThread;					// 描画コマンド記録を担当する独立スレッド
	std::mutex				m_RenderMutex;					// ジョブキューとスレッド待機状態を守るカギ
	std::condition_variable m_RenderCv;						// Renderスレッドを起こすための通知器
	std::condition_variable m_RenderDoneCv;					// Gameスレッドが枠空きを待つための通知器
	std::deque<RenderJob>	m_RenderQueue;					// 描画待ちジョブキュー
	std::atomic<bool>		m_RenderExit{ false };			// エンジン終了要求フラグ
	std::atomic<UINT64>		m_RenderCompletedFrame{ 0 };	// Renderスレッドが描き終えた最新フレーム番号

	/// @brief Renderスレッドのメインループ関数
	void RenderThreadMain();

	/// @brief 1フレーム分の描画コマンドを記録してGPUへ投げる関数
	void ExecuteRenderJob(_In_ const RenderJob& job);

	/// @brief GameスレッドからRenderスレッドへ仕事を渡す
	void KickRender(UINT64 frameNumber);

	/// @brief Gameスレッドが先走りしないようにリングバッファの空きを待つ
	void WaitForRenderSlot(UINT64 frameNumber);

	/// @brief 終了時やリサイズ時に、たまっている描画をすべて完了させる
	void FlushRender();
#endif

	void CreateGameWindow(int width, int height);
};
