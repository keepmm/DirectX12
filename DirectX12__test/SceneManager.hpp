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

	/// @brief シーンの変更
	void ChangeScene(_In_ const std::string& name);

	/// @brief シーンの取得
	Scene* GetCurrentScene() const;

	/// @brief シーンの更新処理
	void Update();

	/// @brief シーンの描画処理
	void Draw(_In_ const RenderContext& renderContext);

	/// @brief ThreadPoolの取得
	ThreadPool& GetThreadPool() { return m_ThreadPool; }

private:
	std::unordered_map<std::string, std::unique_ptr<Scene>> m_Scenes;
	Scene* m_CurrentScene = nullptr;
	std::string m_NextSceneName;
	bool m_SceneChangeRequested = false;
	mutable std::mutex m_SceneMutex;
	ThreadPool m_ThreadPool;

	/// @brief シーンコールバック設定
	void SetupSceneCallbacks();
};