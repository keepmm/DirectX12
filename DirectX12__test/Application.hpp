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
#include "DebugWindow.hpp"
#include <chrono>
#include "Engine/Engine.hpp"

#define APPLICATION Application::GetInstance()

class Application : public Engine
{
public:
	static Application* GetInstance();

	~Application() = default;

private:
	std::unique_ptr<DebugWindow> m_DebugWindow;

	HRESULT OnInit() override;
	void OnUpdate() override;
	void OnShutDown() override;

	Application();
	Application(const Application&) = delete;
	void operator=(const Application&) = delete;
};

