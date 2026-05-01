/*****************************************************************//**
 * \file   imguiinit.cpp
 * \brief  imguiを使用するためのクラス
 * 
 * 作成者 keep
 * 作成日 2026/4/25
 * 更新履歴 4.25 ベースプログラムに合わせて実装
 *		   4.27 dx12用に実装
 * *********************************************************************/
#include "imguiinit.hpp"
#include "DirectX.hpp"

// imgui_impl_win32.h の #if 0 による非表示部分を、呼び出し側で前方宣言を追加する
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool IMGUI::s_Initialized = false;
ComPtr<ID3D12DescriptorHeap> IMGUI::s_SrvHeap = nullptr;

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

	ID3D12DescriptorHeap* heaps[] = { s_SrvHeap.Get() };
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
	ImGui::StyleColorsClassic();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 0.0f;
	style.Colors[ImGuiCol_WindowBg].w = 1.0f;

	// ===========================
	//	 日本語フォントの設定
	// ===========================
	ImFontConfig config;
	config.OversampleH = 2;  // 水平方向のオーバーサンプリング
	config.OversampleV = 1;  // 垂直方向のオーバーサンプリング
	config.PixelSnapH = true; // ピクセルにスナップ

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

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	HRESULT hr = device->CreateDescriptorHeap(&heapDesc,IID_PPV_ARGS(s_SrvHeap.ReleaseAndGetAddressOf()));
	if (FAILED(hr) || s_SrvHeap == nullptr)
	{
		return false;
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
	initInfo.SrvDescriptorHeap = s_SrvHeap.Get();
	initInfo.LegacySingleSrvCpuDescriptor = s_SrvHeap->GetCPUDescriptorHandleForHeapStart();
	initInfo.LegacySingleSrvGpuDescriptor = s_SrvHeap->GetGPUDescriptorHandleForHeapStart();

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

	s_SrvHeap.Reset();
	s_Initialized = false;
}

LRESULT IMGUI::ImGui_WndProHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	return ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
}
// ======================================================
// Shift-JIS文字列をUTF-8に変換してImGuiに表示する関数
// ======================================================
std::string IMGUI::ToUTF8(const char* sjis)
{
	return ShiftJIStoUTF8(sjis);
}