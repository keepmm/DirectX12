#pragma once

class World;

class ScriptHost
{
public:
	static void Open(World* world);
	static void Update(float dt, World* world);
	static void Close();

	static const std::vector<std::string>& GetScriptNames();

	/// @brief シーン遷移に使う SceneManager を渡す
	/// @note Engine のメンバなので、スクリプトからは橋渡し経由でしか触れない
	static void SetSceneManager(_In_ class SceneManager* sceneManager);
	static bool isOpen();
};

