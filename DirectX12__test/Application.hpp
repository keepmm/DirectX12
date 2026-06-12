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

	void SetGameMode(bool gameMode) { m_GameMode = gameMode; }

private:
	std::unique_ptr<EditorWindow> m_EditorWindow;
	D3D12_VIEWPORT m_GameViewport{};
	D3D12_RECT m_GameScissorRect{};
	D3D12_VIEWPORT m_EditorViewport{};
	D3D12_RECT m_EditorScissorRect{};
	HRESULT OnInit() override;
	void OnUpdate() override;
	void OnShutDown() override;
	void OnInitPrefabs();
	void ConfigureContext(RenderContext& renderContext) override;
	void ConfigureContext(
		_In_ RenderContext& renderContext,
		_In_ RenderTexture& renderTexture,
		_In_ D3D12_VIEWPORT& viewport,
		_In_ D3D12_RECT& scissorRect,
		bool isSceneView
		);


	/// @brief exe出力用
	bool m_GameMode = false;

	Application();
	Application(const Application&) = delete;
	void operator=(const Application&) = delete;
};

