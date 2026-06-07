/*****************************************************************//**
 * \file   SceneManager.hpp
 * \brief  シーンを管理するクラス
 * 
 * 作成者 keeeeep
 * 作成日 2026/5/22
 * 更新履歴	5.10 作成
 *			5.22 リファクタリング
 * *********************************************************************/
#pragma once

#include "Scene.hpp"
#include "RenderContext.hpp"
#include "Engine/ThreadPool.hpp"
#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>

class MenuScene;

class SceneManager
{
public:
	SceneManager();
	~SceneManager() = default;

	/// @brief シーンの登録
	void RegisterScene(_In_ const std::string& name, _In_ std::unique_ptr<Scene> scene);

	/// @brief シーン追加ロード
	/// @param name 追加ロードするシーンの名前
	void LoadScene(_In_ const std::string& name);

	/// @brief 
	/// @param name 
	void LoadSceneAdditive(_In_ const std::string& name);

	/// @brief 指定シーンをアンロード
	/// @param name 
	void UnloadScene(_In_ const std::string& name);

	/// @brief 指定名のシーンを取得
	/// @param name 取得するシーン名
	/// @return 指定されたシーンのポインタ
	Scene* GetScene(_In_ const std::string& name)const;

	/// @brief アクティブなシーンを取得
	/// @return アクティブなシーンのポインタ
	Scene* GetActiveScene() const;

	/// @brief シーンの更新処理
	void Update(_In_ float deltatime);
	void FixedUpdate(_In_ float fixedDeltatime);
	void LateUpdate(_In_ float deltatime);

	/// @brief シーンの描画処理
	void Draw(_In_ const RenderContext& renderContext);

	/// @brief ThreadPoolの取得
	ThreadPool& GetThreadPool() { return m_ThreadPool; }

private:
	struct SceneEntry
	{
		std::unique_ptr<Scene> scene;
		bool isLoaded = false;
		bool isPending = false;
	};
	std::unordered_map<std::string,SceneEntry> m_Scenes;
	std::vector<Scene*> m_LoadedScenes; // 複数シーンがアクティブな場合に備えてvectorで管理
	Scene* m_ActiveScene = nullptr;
	std::string m_NextSceneName;
	bool m_SceneChangeRequested = false;

	std::queue<std::string> m_LoadQueue;
	std::queue<std::string> m_UnloadQueue;

	mutable std::mutex m_SceneMutex;
	ThreadPool m_ThreadPool;

	/// @brief シーンコールバック設定
	void ProcessSceneQueue();
	void SetActiveScene(_In_ const std::string& name);
	void UpdateLoadedScenes(_In_ float deltatime);
	void DrawLoadedScenes(_In_ const RenderContext& renderContext);
	Scene* FindLoadedScene(_In_ const std::string& name) const;
};