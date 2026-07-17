#pragma once

#include <string>
#include <atomic>

struct BuildSetting
{
	std::string outputDir = "Build";
	std::string gameName = "MyGame";
	std::string startScene = "SampleScene.json";
	std::string configuration = "Debug";
};

class BuildSystem
{
public:
	static void Build(_In_ const BuildSetting& settings);

	static void Update();

	static bool IsBuilding();

	static float GetProgress();

	static std::string GetStage();
};

