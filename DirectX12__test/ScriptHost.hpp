#pragma once

class World;

class ScriptHost
{
public:
	static void Open(World* world);
	static void Update(float dt, World* world);
	static void Close();

	static const std::vector<std::string>& GetScriptNames();
	static bool isOpen();
};

