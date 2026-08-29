/*****************************************************************//**
 * \file   main.cpp
 * \brief  DirectX12のテストコード
 *
 * 作成者 keeep
 * 作成日 2026/2/11
 * 更新履歴	2/11 ウィンドウの作成
 *			2/12 DirectX12の初期化
 *			2/15 APPLICATIONクラスの作成
 *			5/02 初期化追加
 * *********************************************************************/
#include "Application.hpp"
#include "Defines.hpp"
#include <filesystem>

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
	{
		return 0;
	}

	// exe と同階層の game.cfg があればゲームモード起動
	bool gameMode = (lpCmdLine && _tcsstr(lpCmdLine, _T("-game")) != nullptr);
	{
		wchar_t exePath[MAX_PATH];
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		auto exeDir = std::filesystem::path(exePath).parent_path();
		std::ifstream cfg(exeDir / "game.cfg");
		if (cfg)
		{
			gameMode = true;
			std::string scene, dataDir;
			std::getline(cfg, scene);
			std::getline(cfg, dataDir);
			if (!scene.empty() && scene.back() == '\r')   scene.pop_back();
			if (!dataDir.empty() && dataDir.back() == '\r') dataDir.pop_back();
			auto binDir = exeDir / L"Bin";
			if (std::filesystem::exists(binDir))
				SetDllDirectoryW(binDir.c_str());

			// 相対パス("Assets/...", "Shaders/...")の基準を Data フォルダにする
			if (!dataDir.empty())
				SetCurrentDirectoryW((exeDir / dataDir).c_str());
			if (!scene.empty())
				APPLICATION->SetStartScene(scene);
		}
	}
	APPLICATION->SetGameMode(gameMode);

	APPLICATION->Init(hInstance,WINDOW_WIDTH,WINDOW_HEIGHT);

	APPLICATION->Run();

	CoUninitialize();

	return 0;
}

// 参考リンク : 
// https://qiita.com/sanoh/items/11b339daf2ff3a4d5e88 DirectX12の初期化
// https://qiita.com/dpals39/items/773846ab3c8f9abedc79 シェーダーから描画まで
