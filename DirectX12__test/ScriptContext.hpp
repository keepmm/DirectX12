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
	void (*launchFirework)(float x, float y, float z, int shape,
		float r, float g, float b, const char* text) = nullptr;
};
