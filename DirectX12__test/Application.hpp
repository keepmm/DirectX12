/*****************************************************************//**
 * \file   Application.hpp
 * \brief  アプリケーションのメインクラス
 * 
 * 作成者 
 * 作成日 2026/2/19
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <Windows.h>
#include "Defines.hpp"
#include "DirectX.hpp"
#include "SampleScene.hpp"
#include <chrono>

#define APPLICATION Application::GetInstance()

class Application
{
public:
	static Application* GetInstance();
	HRESULT Init(HINSTANCE hInstance);
	void Run();
	void Terminate();
	SIZE GetWindowSize() const;
	~Application();
private:
	HWND m_hWnd;
	HINSTANCE m_hInstance;

	std::unique_ptr<DirectXApp> m_DirectX;
	std::unique_ptr<SampleScene> m_Scene;

	void CreateGameWindow(HWND& hWnd, WNDCLASSEX& windowClass);

	Application();
	Application(const Application&) = delete;
	void operator=(const Application&) = delete;
};

