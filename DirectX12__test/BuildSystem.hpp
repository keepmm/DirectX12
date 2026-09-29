#pragma once

#include <string>
#include <atomic>

struct BuildSetting
{
	std::string outputDir = "Build";
	std::string gameName = "MyGame";
	std::string startScene = "SampleScene";   // 名前(拡張子なし)。SceneManager::ScenePathFromName で解決する
	std::string configuration = "Debug";

	// 開始シーンから参照されているアセットだけをコピーする。
	// false なら Assets を丸ごとコピー(従来動作)
	bool usedAssetsOnly = true;
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

