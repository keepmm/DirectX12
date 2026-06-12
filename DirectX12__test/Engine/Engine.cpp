#include "Engine.hpp"
#include "../Defines.hpp"
#include "../Logger.hpp"
#include "../Time.hpp"
#include "../Input.hpp"
#include "../imguiinit.hpp"
#include "../imgui-master/backends/imgui_impl_dx12.h"
#include "../imgui-master/backends/imgui_impl_win32.h"

#define CR_HOST CR_UNSAFE
#include "../cr.h"

cr_plugin g_plugin;
ScriptContext g_scriptContext;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (IMGUI::ImGui_WndProHandler(hWnd, msg, wParam, lParam))
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

void Engine::CreateGameWindow(int width, int height)
{
	WNDCLASSEX windowClass;
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

	RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
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

HRESULT Engine::Init(HINSTANCE hInstance, int width, int height)
{
	m_hInstance = hInstance;
	ImGui_ImplWin32_EnableDpiAwareness();
	CreateGameWindow(width, height);

	LOG->Init();
	m_DirectX = MakeUnique<DirectXApp>(m_hWnd, width, height);

	TIME->Init();
	INPUT->Init(m_hWnd);

	if (!IMGUI::Start(m_hWnd, m_DirectX->GetDevice().Get(), m_DirectX->GetCommandQueue().Get(),RTV_NUM, DXGI_FORMAT_R8G8B8A8_UNORM))
	{
		MessageBox(NULL, _T("IMGUIの初期化に失敗"), PROC_NAME, MB_OK);
		return E_FAIL;
	}

	UpdateWindow(m_hWnd);
	ShowWindow(m_hWnd, SW_SHOW);

	// サブクラスの初期化
	HRESULT hr = OnInit();
	if (FAILED(hr))
	{
		return hr;
	}

	return S_OK;
}

void Engine::Run()
{
	MSG msg = {};
	constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
	float accumulatedTime = 0.0f;
	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else
		{
			TIME->Update();
			INPUT->Update();

			float deltaTime = TIME->GetDeltaTime();

			m_DirectX->ReloadShader();
			if (FAILED(m_DirectX->BeginRender()))
			{
				// 失敗時は次フレームへ
				return;
			}
			IMGUI::BeginFrame();

			// エンジン更新
			m_SceneManager.Update(deltaTime);


			// 固定タイムステップ更新
			accumulatedTime += deltaTime;
			while (accumulatedTime >= FIXED_TIMESTEP)
			{
				m_SceneManager.FixedUpdate(FIXED_TIMESTEP);
				accumulatedTime -= FIXED_TIMESTEP;
			}

			m_SceneManager.LateUpdate(deltaTime);

			OnUpdate();

			// 描画コンテキスト作成
			RenderContext renderContext{};
			renderContext.CommandList = m_DirectX->GetCommandList().Get();
			renderContext.frameIndex = m_DirectX->GetFrameIndex();
			renderContext.cbAllocator = &m_DirectX->GetConstantBufferAllocator();

			const auto& settings = RenderSettings::Get();
			renderContext.vertexShader = settings.vertexShader;
			renderContext.pixelShader = settings.pixelShader;
			renderContext.wireframe = settings.wireframe;
			renderContext.meshShaderSupported = m_DirectX->IsMeshShaderSupported();
			renderContext.useMeshShader = settings.meshShader && renderContext.meshShaderSupported;
			renderContext.meshShaderPso = renderContext.useMeshShader ? m_DirectX->GetMeshPso() : nullptr;
			renderContext.CommandList6 = m_DirectX->GetCommandList6();

			// シーン描画
			ConfigureContext(renderContext);
			m_SceneManager.Draw(renderContext);
			m_DirectX->Present();

			IMGUI::EndFrame(renderContext.CommandList);
			if(FAILED(m_DirectX->EndRender()))
			{
				// 失敗時は次フレームへ
				return;
			}	
		}
	}
}

void Engine::Terminate()
{
	OnShutDown();
	IMGUI::Release();
	LOG->ShutDown();
	DestroyWindow(m_hWnd);
}