#pragma once

#include <vector>
#include <string>
#include <cstdint>

class World;

/// @brief Raycast の結果(DLL と exe でやり取りする生の形)
struct RaycastHitRaw
{
	std::uint32_t entity = 0;
	float point[3]{};
	float normal[3]{};
	float distance = 0.0f;
};

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

	// 物理。PhysicsWorld は Scene のメンバで DLL から触れないので橋渡しする
	bool (*raycast)(const float* origin, const float* direction, float maxDistance,
		std::uint32_t layerMask, RaycastHitRaw* outHit) = nullptr;
	int  (*overlapSphere)(const float* center, float radius, std::uint32_t layerMask,
		std::uint32_t* outEntities, int maxCount) = nullptr;
	bool (*addForce)(std::uint32_t entity, float x, float y, float z, bool impulse) = nullptr;
	bool (*setVelocity)(std::uint32_t entity, float x, float y, float z) = nullptr;
	bool (*getVelocity)(std::uint32_t entity, float* outVelocity) = nullptr;
};
