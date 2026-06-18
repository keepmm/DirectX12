#include "SceneManager.hpp"
#include "Logger.hpp"
#include "PlayState.hpp"
#include "RuntimeScene.hpp"
#include "SceneSerializer.hpp"

SceneManager::SceneManager()
	: m_ThreadPool(std::thread::hardware_concurrency())
{
}

void SceneManager::RegisterScene(const std::string& name, std::unique_ptr<Scene> scene)
{
	// 非同期処理をロック
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	if (m_Scenes.find(name) != m_Scenes.end())
	{
		LOG->LogWarning("Scene '" + name + "'は既に登録されています");
		return;
	}
	// シーン名を設定して登録
	scene->SetSceneName(name);
	m_Scenes[name].scene = std::move(scene);
	LOG->LogInfo("Scene '" + name + "'を登録しました");
}

void SceneManager::LoadScene(const std::string& name)
{
	// 非同期処理をロック
	//std::unique_lock<std::mutex> lock(m_SceneMutex);

	// シーンが登録されているか確認
	if (m_Scenes.find(name) == m_Scenes.end())
	{
		LOG->LogError("Scene '" + name + "'は登録されていません");
		return;
	}

	// 現在のシーンをアンロード
	if (m_ActiveScene)
	{
		m_UnloadQueue.push(m_ActiveScene->GetSceneName());
	}

	// 新しいシーンのロード
	m_LoadQueue.push(name);
	m_NextSceneName = name;
	m_SceneChangeRequested = true;

	LOG->LogInfo("Scene '" + name + "'のロードが要求されました");
}

void SceneManager::LoadSceneAdditive(const std::string& name)
{
	// 非同期処理をロック
	std::unique_lock<std::mutex> lock(m_SceneMutex);

	if(m_Scenes.find(name) == m_Scenes.end())
	{
		LOG->LogError("Scene '" + name + "'は登録されていません");
		return;
	}

	m_LoadQueue.push(name);
	LOG->LogInfo("Scene '" + name + "'の追加ロードが要求されました");
}

void SceneManager::UnloadScene(const std::string& name)
{
	// 非同期処理をロック
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	if(m_Scenes.find(name) == m_Scenes.end())
	{
		LOG->LogError("Scene '" + name + "'は登録されていません");
		return;
	}
	m_UnloadQueue.push(name);
	LOG->LogInfo("Scene '" + name + "'のアンロードが要求されました");
}

Scene* SceneManager::GetScene(const std::string& name) const
{
	// 非同期処理をロック
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	auto it = m_Scenes.find(name);
	if (it != m_Scenes.end())
	{
		return it->second.scene.get();
	}
	return nullptr;
}

Scene* SceneManager::GetActiveScene() const
{
	// 非同期処理をロック
	std::unique_lock<std::mutex> lock(m_SceneMutex);
	return m_ActiveScene;
}

void SceneManager::Update(float deltatime)
{
	// 非同期処理をロック
	std::unique_lock<std::mutex> lock(m_SceneMutex);

	// シーンロード / アンロード処理
	ProcessSceneQueue();

	// ふぇーふぉ処理
	UpdateFade(deltatime);

	// ロード済みシーンの更新
	UpdateLoadedScenes(deltatime);
}

void SceneManager::Draw(const RenderContext& renderContext)
{
	if (m_ActiveScene && m_ActiveScene->IsActive())
	{
		m_ActiveScene->Draw(renderContext);
	}
}

void SceneManager::RequestSceneChangeWithFade(const std::string& name)
{
	if (m_Transition != TransitionPhase::None) return;
	m_PendingSceneName = name;
	m_Transition = TransitionPhase::FadeOut;
}

void SceneManager::RequestSceneChangeWithString(const std::string& name)
{
	if (m_Transition != TransitionPhase::None) return;
	m_PendingScenePath = name;
	m_Transition = TransitionPhase::FadeOut;
}

void SceneManager::ProcessSceneQueue()
{
	// アンロード処理
	while (!m_UnloadQueue.empty())
	{
		std::string sceneName = m_UnloadQueue.front();
		m_UnloadQueue.pop();

		Scene* scene = FindLoadedScene(sceneName);
		if (scene)
		{
			scene->SetState(SceneState::Unloading);
			scene->OnUnload();
			scene->SetState(SceneState::Unloaded);

			// ロード済みリストから削除
			auto it = std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene);
			if (it != m_LoadedScenes.end())
			{
				m_LoadedScenes.erase(it);
			}

			if (m_ActiveScene == scene)
			{
				m_ActiveScene = nullptr;
			}

			LOG->LogInfo("Scene '" + sceneName + "'がアンロードされました");
		}
	}

	// ロード処理
	while (!m_LoadQueue.empty())
	{
		std::string sceneName = m_LoadQueue.front();
		m_LoadQueue.pop();

		auto it = m_Scenes.find(sceneName);
		if (it != m_Scenes.end())
		{

			Scene* scene = it->second.scene.get();
			if (scene->GetState() == SceneState::Unloaded)
			{
				scene->SetState(SceneState::Loading);
				scene->OnLoad();
				scene->SetState(SceneState::Active);
				m_LoadedScenes.push_back(scene);
				SetActiveScene(sceneName);
				LOG->LogInfo("Scene '" + sceneName + "'がロードされました");
			}

			if (m_SceneChangeRequested && sceneName == m_NextSceneName)
			{
				SetActiveScene(sceneName);
				m_SceneChangeRequested = false;
			}
			else if (!m_SceneChangeRequested)
			{
				// Additiveロードの場合はアクティブシーンを変更しない
				if (std::find(m_LoadedScenes.begin(), m_LoadedScenes.end(), scene) == m_LoadedScenes.end())
				{
					m_LoadedScenes.push_back(scene);
					scene->SetState(SceneState::Active);
					scene->OnStart();
					LOG->LogInfo("Scene '" + sceneName + "'が追加ロードされました");
				}
			}
		}
	}
}

void SceneManager::SetActiveScene(const std::string& name)
{
	Scene* scene = FindLoadedScene(name);
	if (!scene)
	{
		auto it = m_Scenes.find(name);
		if(it != m_Scenes.end())
		{
			scene = it->second.scene.get();
		}
	}

	if (scene)
	{
		// 現在のアクティブシーンを一時停止
		if (m_ActiveScene && m_ActiveScene != scene)
		{
			m_ActiveScene->SetState(SceneState::Paused);
			m_ActiveScene->OnPause();
			LOG->LogInfo("Scene '" + m_ActiveScene->GetSceneName() + "'が一時停止されました");
		}

		// 新しいシーンをアクティブにする
		if(std::find(m_LoadedScenes.begin(),m_LoadedScenes.end(),scene) == m_LoadedScenes.end())
		{
			m_LoadedScenes.push_back(scene);
		}

		m_ActiveScene = scene;
		m_ActiveScene->SetState(SceneState::Active);
		m_ActiveScene->OnStart();
		m_ActiveScene->EnsurePhysicsWorld();

		LOG->LogInfo("Scene '" + name + "'がアクティブになりました");
	}
}

void SceneManager::UpdateLoadedScenes(float deltatime)
{
	for (auto scene : m_LoadedScenes)
	{
		if(scene && scene->IsActive())
		{
			if (PLAY.isPlaying())
			{
				scene->Update(deltatime);
			}
			else
			{
				static_cast<RuntimeScene*>(scene)->EditorUpdate(deltatime);
			}
		}
	}
}

void SceneManager::DrawLoadedScenes(const RenderContext& renderContext)
{
	if (m_ActiveScene)
	{
		m_ActiveScene->Draw(renderContext);
	}
}

Scene* SceneManager::FindLoadedScene(const std::string& name) const
{
	for(auto scene : m_LoadedScenes)
	{
		if(scene && scene->GetSceneName() == name)
		{
			return scene;
		}
	}
	return nullptr;
}

void SceneManager::FixedUpdate(float fixedDeltatime)
{
	if(!PLAY.isPlaying())
	{
		return;
	}

	for (auto scene : m_LoadedScenes)
	{
		if(scene && scene->IsActive())
		{
			if (auto* physicsWorld = scene->GetPhysicsWorld())
			{
				physicsWorld->Update(fixedDeltatime);
				physicsWorld->SyncTransforms(scene->GetWorld());
			}
			scene->FixedUpdate(fixedDeltatime);
		}
	}
}

void SceneManager::LateUpdate(float deltatime)
{
	for (auto scene : m_LoadedScenes)
	{
		if(scene && scene->IsActive())
		{
			scene->LateUpdate(deltatime);
		}
	}
}

void SceneManager::UpdateFade(float deltatime)
{
	switch (m_Transition)
	{
	case TransitionPhase::FadeOut:
		m_FadeAlpha += m_FadeSpeed * deltatime;
		if (m_FadeAlpha >= 1.0f)
		{
			m_FadeAlpha = 1.0f;

			// 真っ黒の瞬間に実処理（どちらの遷移かで分岐）
			if (!m_PendingScenePath.empty())
			{
				// 方式A: アクティブシーンにファイルから上書きロード
				if (m_ActiveScene)
				{
					APP->WaitForGPUIdle();
					SceneSerializer::Load(*m_ActiveScene, m_PendingScenePath);
				}
				m_PendingScenePath.clear();
			}
			else if (!m_PendingSceneName.empty())
			{
				// 名前指定の切り替え
				LoadScene(m_PendingSceneName);
				m_PendingSceneName.clear();
			}

			m_Transition = TransitionPhase::FadeIn;
		}
		break;
	case TransitionPhase::FadeIn:
		m_FadeAlpha -= m_FadeSpeed * deltatime;
		if (m_FadeAlpha <= 0.0f)
		{
			m_FadeAlpha = 0.0f;
			m_Transition = TransitionPhase::None;
		}
		break;
	case TransitionPhase::None:
		break;
	default:
		break;
	}
}
