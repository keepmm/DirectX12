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

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
	{
		return 0;
	}

	APPLICATION->Init(hInstance,720,480);

	APPLICATION->Run();

	CoUninitialize();

	return 0;
}

// 参考リンク : 
// https://qiita.com/sanoh/items/11b339daf2ff3a4d5e88 DirectX12の初期化
// https://qiita.com/dpals39/items/773846ab3c8f9abedc79 シェーダーから描画まで
