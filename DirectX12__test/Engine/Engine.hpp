/*****************************************************************//**
 * \file   Engine.hpp
 * \brief　エンジンに使用するクラスの宣言
 * 
 * 作成者 keep
 * 作成日 2026/5/15
 * 更新履歴	5.15 作成
 * *********************************************************************/
#pragma once

#include "../SceneManager.hpp"
#include "../DirectX.hpp"

class Engine
{
public:
	virtual ~Engine() = default;

	HRESULT Init(
		_In_ HINSTANCE hInstance,
		_In_ int width,
		_In_ int height);
	void Run();
	void Terminate();

	// ---- サブクラスがオーバーライドする仮想関数 ---- //

	virtual HRESULT OnInit() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnShutDown() = 0;

protected:
	SceneManager m_SceneManager;
	std::unique_ptr<DirectXApp> m_DirectX;
	HWND m_hWnd = nullptr;
	HINSTANCE m_hInstance = nullptr;

	void CreateGameWindow(int width, int height);
};

