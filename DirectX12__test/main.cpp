/*****************************************************************//**
 * \file   main.cpp
 * \brief  DirectX12のテストコード
 *
 * 作成者 murao
 * 作成日 2026/2/11
 * 更新履歴	2/11 ウィンドウの作成
 *			2/12 DirectX12の初期化
 *			2/15 APPLICATIONクラスの作成
 * *********************************************************************/
#include "Application.hpp"

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	APPLICATION->Init(hInstance);

	APPLICATION->Run();

	return 0;
}

// 参考リンク : 
// https://qiita.com/sanoh/items/11b339daf2ff3a4d5e88 DirectX12の初期化
// https://qiita.com/dpals39/items/773846ab3c8f9abedc79 シェーダーから描画まで
