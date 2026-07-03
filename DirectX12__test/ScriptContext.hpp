#pragma once

#include <vector>
#include <string>

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
};
