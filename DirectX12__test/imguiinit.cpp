/*****************************************************************//**
 * \file   imguiinit.cpp
 * \brief  imguiを使用するためのクラス
 * 
 * 作成者 keep
 * 作成日 2026/4/25
 * 更新履歴 4.25 ベースプログラムに合わせて実装
 *		   4.27 dx12用に実装
 *			6.4 ディスクリプタヒープをDirectXAppから割り当てるように実装
 * *********************************************************************/
#include "imguiinit.hpp"
#include "DirectX.hpp"
#include <string_view>
#include "Theme.hpp"
#include <cstdlib>

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// static変数初期化
bool IMGUI::s_Initialized = false;

// Shift-JIS文字列をUTF-8に変換するヘルパー関数
static std::string ShiftJIStoUTF8(const std::string& sjis)
{
	// Shift-JISからワイド文字（UTF-16）に変換
	int wideSize = MultiByteToWideChar(CP_ACP, 0, sjis.c_str(), -1, nullptr, 0);
	if (wideSize == 0) return sjis;

	std::wstring wideStr(wideSize, 0);
	MultiByteToWideChar(CP_ACP, 0, sjis.c_str(), -1, &wideStr[0], wideSize);

	// ワイド文字（UTF-16）からUTF-8に変換
	int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (utf8Size == 0) return sjis;

	std::string utf8Str(utf8Size, 0);
	WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], utf8Size, nullptr, nullptr);

	return utf8Str;
}

void ImGuiSrvAlloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
{
	auto& alloc = DirectXApp::GetCurrent()->GetSrvAllocator();
	UINT idx = 0;
	if (alloc.Allocate(idx))
	{
		*outCpuHandle = alloc.Cpu(idx);
		*outGpuHandle = alloc.Gpu(idx);
	}
}
void ImGuiSrvFree(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)
{
}

void IMGUI::BeginFrame()
{
#if DebugSwitch
	if (!s_Initialized) return;
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();
#endif
}

void IMGUI::EndFrame(ID3D12GraphicsCommandList* commandList)
{
#if DebugSwitch
	// 描画コマンドリストが有効でない場合は終了
	if (!s_Initialized || commandList == nullptr) return;

	ImGui::Render();

	auto* app = DirectXApp::GetCurrent();
	auto rtv = app->GetRtvHandle();
	auto dsv = app->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	ID3D12DescriptorHeap* heaps[] = { app->GetSrvHeap() };
	commandList->SetDescriptorHeaps(1, heaps);

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
#endif
}

bool IMGUI::Start(_In_ HWND hWnd,
	_In_ ID3D12Device* device,
	_In_ ID3D12CommandQueue* commandQueue,
	int framecount,
	DXGI_FORMAT rtvformat)
{
#if !DebugSwitch
	return true;
#endif
	if (s_Initialized)return true;
	if (hWnd == nullptr || device == nullptr || framecount <= 0) return false;
	if (commandQueue == nullptr) return false;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // ドッキング有効化

	//Viewportsを有効にしてウィンドウをホストの外に出せるようにする
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	// スタイル調整
	ApplyTheme(uiTheme::Dark);
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 0.0f;
	style.Colors[ImGuiCol_WindowBg].w = 1.0f;

	// ===========================
	//	 日本語フォントの設定
	// ===========================
	ImFontConfig config;
	auto* jp = io.Fonts->GetGlyphRangesJapanese();
	config.OversampleH = 2;  // 水平方向のオーバーサンプリング
	config.OversampleV = 1;  // 垂直方向のオーバーサンプリング
	config.PixelSnapH = true; // ピクセルにスナップ

	auto addFont = [&](const char* path, float size, const char* name) -> ImFont*
		{
			if (!std::filesystem::exists(path)) return nullptr;   // 無ければスキップ
			strcpy_s(config.Name, name);
			return io.Fonts->AddFontFromFileTTF(path, size, &config, jp);
		};

	// ユーザーフォントフォルダのパスを組み立て
	std::string userFonts;
	if (const char* local = std::getenv("LOCALAPPDATA"))
		userFonts = std::string(local) + "\\Microsoft\\Windows\\Fonts\\";

	ImFont* first = nullptr;
	auto pick = [&](ImFont* f) { if (f && !first) first = f; };

	// うずら（ユーザーフォント）
	pick(addFont((userFonts + "uzura.ttf").c_str(), 16.0f, "うずら 16px"));
	pick(addFont((userFonts + "uzura.ttf").c_str(), 20.0f, "うずら 20px"));
	// システムフォント
	pick(addFont("C:\\Windows\\Fonts\\meiryo.ttc", 18.0f, "メイリオ 18px"));
	pick(addFont("C:\\Windows\\Fonts\\YuGothM.ttc", 18.0f, "游ゴシック 18px"));

	io.FontDefault = first ? first : io.Fonts->AddFontDefault();

	// ========================================
	// Windowsの標準日本語フォントを読み込む
	// ========================================
	ImFont* font = io.Fonts->AddFontFromFileTTF(
		"C:\\Windows\\Fonts\\msgothic.ttc",
		18.0f,
		&config,
		io.Fonts->GetGlyphRangesJapanese()
	);

	// ==========================================================
	// フォントが読み込めなかった場合はデフォルトフォントを使用
	// ==========================================================
	if (font == nullptr)
	{
		io.Fonts->AddFontDefault();
	}

	if (!ImGui_ImplWin32_Init(hWnd))
	{
		return false;
	}

	// DX12での初期化
	ImGui_ImplDX12_InitInfo initInfo = {};
	initInfo.Device = device;
	initInfo.CommandQueue = commandQueue;
	initInfo.NumFramesInFlight = framecount;
	initInfo.RTVFormat = rtvformat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = DirectXApp::GetCurrent()->GetSrvAllocator().heap.Get();
	initInfo.SrvDescriptorAllocFn = ImGuiSrvAlloc;
	initInfo.SrvDescriptorFreeFn = ImGuiSrvFree;

	if(!ImGui_ImplDX12_Init(&initInfo))
	{
		return false;
	}

	s_Initialized = true;
	return true;
}

void IMGUI::Release()
{
#if !DebugSwitch
	return;
#endif
	if (!s_Initialized) return;

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	s_Initialized = false;
}

LRESULT IMGUI::ImGui_WndProHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}

std::string IMGUI::ToUTF8(std::string text)
{
	return ShiftJIStoUTF8(text);
}

ImVec2 IMGUI::GetMousePosInViewPort()
{
	ImVec2 pos = ImGui::GetMousePos();
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	if (viewport != nullptr)
	{
		pos.x -= viewport->WorkPos.x;
		pos.y -= viewport->WorkPos.y;
	}
	return pos;
}
