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
#include "Engine/Engine.hpp"
#include "EditorWindow.hpp"

#define APPLICATION Application::GetInstance()

class Application : public Engine
{
public:
	static Application* GetInstance();

	~Application() = default;

private:
	std::unique_ptr<EditorWindow> m_EditorWindow;
	D3D12_VIEWPORT m_GameViewport{};
	D3D12_RECT m_GameScissorRect{};
	HRESULT OnInit() override;
	void OnUpdate() override;
	void OnShutDown() override;
	void OnInitPrefabs();
	void ConfigureContext(RenderContext& renderContext) override;

	Application();
	Application(const Application&) = delete;
	void operator=(const Application&) = delete;
};

