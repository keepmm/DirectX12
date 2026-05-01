#include "Application.hpp"
#include "Defines.hpp"
#include "Polygon.hpp"
#include "Time.hpp"
#include "Input.hpp"
#include "Logger.hpp"
#include "imguiinit.hpp"
#include "imgui-master/backends/imgui_impl_dx12.h"
#include "imgui-master/backends/imgui_impl_win32.h"

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if(IMGUI::ImGui_WndProHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

void Application::CreateGameWindow(HWND& hWnd, WNDCLASSEX& windowClass)
{
	ZeroMemory(&windowClass, sizeof(WNDCLASSEX));
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.lpfnWndProc = WndProc;
	windowClass.cbClsExtra = 0;
	windowClass.cbWndExtra = 0;
	windowClass.hInstance = m_hInstance;
	windowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	windowClass.lpszMenuName = NULL;
	windowClass.lpszClassName = CLASS_NAME;

	windowClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	if (RegisterClassEx(&windowClass) == 0)
	{
		MessageBox(NULL, _T("ウィンドウクラスの登録に失敗"), PROC_NAME, MB_OK);
		return;
	}

	RECT rect =
	{
		(LONG)0,
		(LONG)0,
		(LONG)(WINDOW_WIDTH),
		(LONG)(WINDOW_HEIGHT),
	};

	AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);

	m_hWnd = CreateWindow(
		CLASS_NAME,
		PROC_NAME,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		NULL, NULL, m_hInstance, NULL
	);
}

Application::Application() :
	m_hWnd(nullptr),
	m_hInstance(nullptr)
{
}

Application::~Application()
{
}

Application* Application::GetInstance()
{
	static Application instance;
	return &instance;
}

HRESULT Application::Init(HINSTANCE hInstance)
{
	m_hInstance = hInstance;

	WNDCLASSEX windowClass;
	CreateGameWindow(m_hWnd, windowClass);
	LOG->Init();
	m_DirectX = MakeUnique<DirectXApp>(m_hWnd, WINDOW_WIDTH, WINDOW_HEIGHT);
	m_Scene = MakeUnique<SampleScene>(
		m_DirectX->GetDevice(),
		m_DirectX->GetPipelineStates(),
		m_DirectX->GetPipelineStateWireFrame());

	Polygon::Init(
		m_DirectX->GetDevice(),
		m_DirectX->GetCommandList(),
		m_DirectX->GetPipelineState(E_VERTEX_SHADER::BASIC, E_PIXEL_SHADER::BASIC),
		m_DirectX->GetPipelineStateWireFrame());
	Polygon::CreatePolygon();

	TIME->Init();
	INPUT->Init(m_hWnd);
	m_DebugWindow = MakeUnique<DebugWindow>();

	if(!IMGUI::Start(m_hWnd, m_DirectX->GetDevice().Get(), m_DirectX->GetCommandQueue().Get(), DirectXApp::RTV_NUM, DXGI_FORMAT_R8G8B8A8_UNORM))
	{
		MessageBox(NULL, _T("IMGUIの初期化に失敗"), PROC_NAME, MB_OK);
		return E_FAIL;
	}

	UpdateWindow(m_hWnd);
	ShowWindow(m_hWnd, SW_SHOW);
	return S_OK;
}

void Application::Run()
{
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			/* タイマー更新*/
			TIME->Update();

			/* 入力更新 */
			INPUT->Update();

			/* DirectX描画開始 */
			m_DirectX->BeginRender();

			/* ImGui描画開始 */
			IMGUI::BeginFrame();

			/* 更新 */
			m_Scene->Update();

			/* 描画 */
			RenderContext renderContext{};
			renderContext.CommandList = m_DirectX->GetCommandList().Get();
			renderContext.frameIndex = m_DirectX->GetFrameIndex();

			const auto& settings = RenderSettings::Get();
			renderContext.vertexShader = settings.vertexShader;
			renderContext.pixelShader = settings.pixelShader;
			renderContext.wireframe = settings.wireframe;
			m_Scene->Draw(renderContext);

			/* デバッグウィンドウ描画 */
			m_DebugWindow->Draw();

			/* ImGui描画終了 */
			IMGUI::EndFrame(renderContext.CommandList);

			/* DirectX描画終了 */
			m_DirectX->EndRender();
		}
	}
}

void Application::Terminate()
{
	IMGUI::Release();
	LOG->ShutDown();
	DestroyWindow(m_hWnd);
}

SIZE Application::GetWindowSize() const
{
	SIZE size = { WINDOW_WIDTH, WINDOW_HEIGHT };
	return size;
}
