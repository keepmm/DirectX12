#include "Engine.hpp"
#include "../Defines.hpp"
#include "../Logger.hpp"
#include "../Time.hpp"
#include "../Input.hpp"
#include "../PlayState.hpp"
#include "../imguiinit.hpp"
#include "../imgui-master/backends/imgui_impl_dx12.h"
#include "../imgui-master/backends/imgui_impl_win32.h"
#include "../ScriptHost.hpp"
#include "../IconLibrary.hpp"
#include "../Profiler.hpp"
#include "../GpuProfiler.hpp"
#include "../DragFiles.hpp"

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (IMGUI::ImGui_WndProHandler(hWnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
	case WM_DROPFILES:
	{
		HDROP drop = reinterpret_cast<HDROP>(wParam);

		const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
		std::vector<std::string> paths;
		paths.reserve(count);

		for (UINT i = 0; i < count; ++i)
		{
			const UINT len = DragQueryFileW(drop, i, nullptr, 0);
			std::wstring w(len, L'\0');
			DragQueryFileW(drop, i, w.data(), len + 1);
			paths.push_back(std::filesystem::path(w).string());
		}

		POINT pt{};
		DragQueryPoint(drop, &pt);   // クライアント座標
		DragFinish(drop);

		DropFiles::Get().Push(std::move(paths), pt.x, pt.y);
		return 0;
	}

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

	// ------------------------ //
	// ボーダレスフルスクリーン //
	// ------------------------ //
	const int screenW = GetSystemMetrics(SM_CXSCREEN);
	const int screenH = GetSystemMetrics(SM_CYSCREEN);

	m_hWnd = CreateWindow(
		CLASS_NAME,
		PROC_NAME,
		WS_POPUP,
		0,0,
		screenW,screenH,
		NULL, NULL, m_hInstance, NULL
	);

	// エクスプローラーからのドロップを受け付ける
	DragAcceptFiles(m_hWnd, TRUE);
}

HRESULT Engine::Init(HINSTANCE hInstance, int width, int height)
{
	m_hInstance = hInstance;
	ImGui_ImplWin32_EnableDpiAwareness();
	CreateGameWindow(width, height);

	LOG->Init();

	// ウィンドウの実サイズでスワップチェーンを作る
	RECT rc{};
	GetClientRect(m_hWnd, &rc);
	const int clientW = rc.right - rc.left;
	const int clientH = rc.bottom - rc.top;

	m_DirectX = MakeUnique<DirectXApp>(m_hWnd, clientW, clientH);

	TIME->Init();
	INPUT->Init(m_hWnd);

	if (!IMGUI::Start(m_hWnd, m_DirectX->GetDevice().Get(), m_DirectX->GetCommandQueue().Get(),RTV_NUM, DXGI_FORMAT_R8G8B8A8_UNORM))
	{
		MessageBox(NULL, _T("IMGUIの初期化に失敗"), PROC_NAME, MB_OK);
		return E_FAIL;
	}

	UpdateWindow(m_hWnd);
	ShowWindow(m_hWnd, SW_SHOW);

#ifdef _FRAMEPIPELINE
	for(auto& frame : m_FramePipeline)
	{
		// FO_DrawItem がボーンパレット(1体 32KB)とモーフを載せるので広めに取る。
		// 足りないと FrameAllocator の assert に落ちる
		frame.Init(32 * 1024 * 1024);
	}
#endif

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
#ifdef _FRAMEPIPELINE
	UINT64 frameNumber = 0;
#endif
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
#ifdef _FRAMEPIPELINE
			if(FAILED(m_DirectX->BeginFrameRecord(frameNumber)))
			{
				// 失敗時は次フレームへ
				return;
			}
#else
			{
				// このスロットのGPU完了待ち。ここが大きい = GPUが追いついていない
				PROFILE_SCOPE("Wait GPU(BeginRender)");
				if (FAILED(m_DirectX->BeginRender()))
				{
					// 失敗時は次フレームへ
					return;
				}
			}
#endif
#ifdef _FRAMEPIPELINE
			// Game フェーズより前にスコープを張る。
			// シーン側(RuntimeScene::PublishFrameObjects)が更新の最後に
			// カメラ/ライトをこのパイプラインへ積むため
			FramePipeline& fp = m_FramePipeline[frameNumber % RTV_NUM];
			fp.Reset(frameNumber);
			FramePipelineScope fpscope(&fp);
#endif
			Profiler::Get().BeginFrame();
			IMGUI::BeginFrame();
			IconLibrary::Get()->BeginFrame();
			ImGuiIO& io = ImGui::GetIO();
			INPUT->SetImGuiCapture(io.WantCaptureKeyboard, io.WantCaptureMouse, io.WantTextInput);

			// ESC でポーズ切り替え。
			// ポーズ中はスクリプトが止まる(RuntimeScene::Update が呼ばれない)ので、
			// スクリプト側では自力で復帰できない。エンジン側で拾う必要がある
			{
				const EngineMode mode = PLAY.GetCurrentMode();
				if (mode != EngineMode::EDITOR && INPUT->GetKeyDown(VK_ESCAPE))
				{
					PLAY.SetMode(mode == EngineMode::PAUSE
						? EngineMode::Play : EngineMode::PAUSE);
				}
			}

			// エンジン更新
			{ PROFILE_SCOPE("Scene::Update"); m_SceneManager.Update(deltaTime); }

			// シーン切り替えの最中はアクティブシーンが一瞬 nullptr になる。
			// アンロードで m_ActiveScene がクリアされ、次のロードで入り直すため
			Scene* scene = m_SceneManager.GetActiveScene();
			if (scene != nullptr)
			{
				PROFILE_SCOPE("ScriptHost");
				ScriptHost::Update(deltaTime, &scene->GetWorld());
			}


			// 固定タイムステップ更新
			accumulatedTime += deltaTime;
			while (accumulatedTime >= FIXED_TIMESTEP)
			{
				m_SceneManager.FixedUpdate(FIXED_TIMESTEP);
				accumulatedTime -= FIXED_TIMESTEP;
			}

			{ PROFILE_SCOPE("Scene::LateUpdate"); m_SceneManager.LateUpdate(deltaTime); }

			OnUpdate();

#ifdef _FRAMEPIPELINE
			{
				const auto& s = RenderSettings::Get();
				fp.AddFrameObject<FO_RenderSettings>(FO_RenderSettings{ s.vertexShader, s.pixelShader, s.wireframe, s.meshShader });
			}
#endif

			RenderContext renderContext{};
			renderContext.CommandList = m_DirectX->GetCommandList().Get();
			renderContext.frameIndex = m_DirectX->GetFrameSlot();
			renderContext.cbAllocator = &m_DirectX->GetConstantBufferAllocator();

			// 描画コンテキスト作成
#ifdef _FRAMEPIPELINE
			const FO_RenderSettings& settings = *fp.GetFrameObject<FO_RenderSettings>();
			fp.FixFrameObject<FO_RenderSettings>();
#else
			const auto& settings = RenderSettings::Get();
#endif
			renderContext.vertexShader = settings.vertexShader;
			renderContext.pixelShader = settings.pixelShader;
			renderContext.wireframe = settings.wireframe;
			renderContext.meshShaderSupported = m_DirectX->IsMeshShaderSupported();
			renderContext.useMeshShader = settings.meshShader && renderContext.meshShaderSupported;
			renderContext.meshShaderPso = renderContext.useMeshShader ? m_DirectX->GetMeshPso() : nullptr;
			renderContext.CommandList6 = m_DirectX->GetCommandList6();

			// シーン描画
			ConfigureContext(renderContext);
			if (renderContext.drawScene)
			{
				PROFILE_SCOPE("Scene::Draw");
				m_SceneManager.Draw(renderContext);
			}
			{
				PROFILE_SCOPE("Present(RT)");
				GPU_PROFILE_SCOPE(renderContext.CommandList, "Present(RT)");
				m_DirectX->Present();
			}

			{
				PROFILE_SCOPE("ImGui");
				GPU_PROFILE_SCOPE(renderContext.CommandList, "ImGui");
				IMGUI::EndFrame(renderContext.CommandList);
			}
#ifdef _FRAMEPIPELINE
			if (FAILED(m_DirectX->CloseFrameRecord()))
			{
				return;
			}
			m_DirectX->KickExecuteAndPresent();	// 非同期投入して即時フレームへ
			++frameNumber;
			// 誰も受け取らなかったドロップは捨てる。
			// 残すと次フレームに別の場所で誤爆する
			DropFiles::Get().Discard();
#else
			{
				// コマンド投入 + 提示。VSync(Present(1,0))の待ちもここに含まれる
				PROFILE_SCOPE("Execute+Present(VSync)");
				if (FAILED(m_DirectX->EndRender()))
				{
					// 失敗時は次フレームへ
					return;
				}
			}	
#endif
		}
	}
}

void Engine::Terminate()
{
	ScriptHost::Close();
	APP->WaitForGPUIdle();
	OnShutDown();
	IMGUI::Release();
	LOG->ShutDown();
	DestroyWindow(m_hWnd);
}