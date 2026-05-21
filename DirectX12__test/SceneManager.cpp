#include "SceneManager.hpp"
#include "MenuScene.hpp"
#include "Logger.hpp"

SceneManager::SceneManager()
	: m_ThreadPool(std::thread::hardware_concurrency())
{
}

void SceneManager::RegisterScene(const std::string& name, std::unique_ptr<Scene> scene)
{
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	if (m_Scenes.find(name) != m_Scenes.end())
	{
		LOG->LogWarning("Scene '" + name + "'は既に登録されています");
		return;
	}
	m_Scenes[name] = std::move(scene);
	LOG->LogInfo("Scene '" + name + "'を登録しました");

	// 最初のシーンを設定
	if (m_CurrentScene == nullptr)
	{
		m_CurrentScene = m_Scenes[name].get();
		LOG->LogInfo("Scene '" + name + "'を現在のシーンに設定しました");
	}
}

void SceneManager::ChangeScene(const std::string& name)
{
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	if (m_Scenes.find(name) == m_Scenes.end())
	{
		LOG->LogError("Scene '" + name + "'は登録されていません");
		return;
	}
	m_NextSceneName = name;
	m_SceneChangeRequested = true;
	LOG->LogInfo("Scene '" + name + "'への変更がリクエストされました");
}

Scene* SceneManager::GetCurrentScene() const
{
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	return m_CurrentScene;
}

void SceneManager::Update()
{
	// シーン遷移を処理
	{
		std::unique_lock<std::mutex> lock(m_SceneMutex);
		if (m_SceneChangeRequested)
		{
			m_CurrentScene = m_Scenes[m_NextSceneName].get();
			m_SceneChangeRequested = false;
			LOG->LogInfo("Scene '" + m_NextSceneName + "'に変更されました");

			// 新しいシーンのコールバックを設定
			SetupSceneCallbacks();
		}
	}

	// 現在のシーンを更新
	Scene* scene = m_CurrentScene;
	if (scene)
	{
		scene->Update();
	}
}

void SceneManager::Draw(const RenderContext& renderContext)
{
	Scene* scene = GetCurrentScene();
	if (scene)
	{
		scene->Draw(renderContext);
	}
}

void SceneManager::SetupSceneCallbacks()
{
	Scene* scene = m_CurrentScene;
	if (!scene) return;

	// MenuScene の場合、コールバックを設定
	MenuScene* menuScene = dynamic_cast<MenuScene*>(scene);
	if (menuScene)
	{
		menuScene->SetSceneChangeCallback([this](const std::string& sceneName)
			{
				this->ChangeScene(sceneName);
			});
	}
}