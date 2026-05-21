#include "Application.hpp"
#include "SampleScene.hpp"
#include "MenuScene.hpp"
#include "DebugWindow.hpp"
#include "Polygon.hpp"
#include "PhysicsScene.hpp"

Application::Application()
{
}

Application* Application::GetInstance()
{
	static Application instance;
	return &instance;
}

HRESULT Application::OnInit()
{
	m_DebugWindow = MakeUnique<DebugWindow>();

	auto menuScene = MakeUnique<MenuScene>();
	m_SceneManager.RegisterScene("MenuScene", std::move(menuScene));

	auto physicsScene = MakeUnique<PhysicsScene>(
		m_DirectX->GetDevice(),
		m_DirectX->GetPipelineStates(),
		m_DirectX->GetPipelineStateWireFrame());
	m_SceneManager.RegisterScene("PhysicsScene", std::move(physicsScene));

	// SampleSceneの登録
	auto sampleScene = MakeUnique<SampleScene>(
		m_DirectX->GetDevice(),
		m_DirectX->GetPipelineStates(),
		m_DirectX->GetPipelineStateWireFrame(),
		m_DirectX->GetLinePso());

	m_SceneManager.RegisterScene("SampleScene", std::move(sampleScene));

	// メニューシーンから開始
	m_SceneManager.ChangeScene("MenuScene");

	// ポリゴン初期化
	Polygon::Init(
		m_DirectX->GetDevice(),
		m_DirectX->GetCommandList(),
		m_DirectX->GetPipelineState(E_VERTEX_SHADER::BASIC, E_PIXEL_SHADER::BASIC),
		m_DirectX->GetPipelineStateWireFrame());
	Polygon::CreatePolygon();

	return S_OK;
}

void Application::OnUpdate()
{
	if (m_DebugWindow)
	{
		m_DebugWindow->Draw();
	}
}

void Application::OnShutDown()
{
}