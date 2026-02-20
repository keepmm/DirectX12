#include "Application.hpp"
#include "Defines.hpp"
#include "Polygon.hpp"

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
		MessageBox(NULL, _T("ƒEƒBƒ“ƒhƒEƒNƒ‰ƒX‚Ì“o˜^‚ÉŽ¸”s"), PROC_NAME, MB_OK);
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
	m_DirectX = MakeUnique<DirectXApp>(m_hWnd, WINDOW_WIDTH, WINDOW_HEIGHT);
	m_Scene = MakeUnique<SampleScene>();

	Polygon::Init(m_DirectX->GetDevice(), m_DirectX->GetCommandList(), m_DirectX->GetPipelineState(), m_DirectX->GetPipelineStateWireFrame());
	Polygon::CreatePolygon();

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

		/* DirectX•`‰æŠJŽn */
		m_DirectX->BeginRender();

		/* XV */
		m_Scene->Update();

		/* •`‰æ */
		m_Scene->Draw();

		/* DirectX•`‰æI—¹ */
		m_DirectX->EndRender();
	}
}

void Application::Terminate()
{
	DestroyWindow(m_hWnd);
}

SIZE Application::GetWindowSize() const
{
	SIZE size = { WINDOW_WIDTH, WINDOW_HEIGHT };
	return size;
}
