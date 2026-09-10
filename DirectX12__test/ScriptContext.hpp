#pragma once

#include <vector>
#include <string>
#include <cstdint>

class World;

struct ScriptContext
{
	World* world = nullptr;
	float deltaTime = 0.0f;
	std::vector<std::string>* savedScripts = nullptr;
	bool isPlaing = false;

	void (*logInfo)(const char* msg) = nullptr;
	void (*logWarning)(const char* msg) = nullptr;
	void (*logError)(const char* msg) = nullptr;
	void (*launchFirework)(float x, float y, float z, int shape,
		float r, float g, float b, const char* text) = nullptr;

	// プレハブ生成と破棄。PrefabLibrary / Scene は exe 側の .cpp にあるので橋渡しする
	uint32_t(*instantiate)(const char* prefabName) = nullptr;
	void    (*destroyEntity)(uint32_t entity) = nullptr;

	// シーン遷移。SceneManager は Engine のメンバで DLL から触れない
	void    (*loadScene)(const char* sceneName, bool withFade) = nullptr;
};
