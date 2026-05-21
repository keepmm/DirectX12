#pragma once

#include "Scene.hpp"
#include "Defines.hpp"
#include <functional>

class MenuScene : public Scene
{
public:
	using SceneChangeCallback = std::function<void(const std::string&)>;

	MenuScene();
	~MenuScene() final = default;

	void Update() final;
	void Draw(_In_ const RenderContext& renderContext) final;

	/// @brief シーン遷移コールバックを設定
	void SetSceneChangeCallback(SceneChangeCallback callback)
	{
		m_OnSceneChange = callback;
	}

private:
	SceneChangeCallback m_OnSceneChange;
	float m_elapsedTime = 0.0f;

	void DrawUI();
	void RequestSceneChange(const std::string& sceneName);
};