/*****************************************************************//**
 * \file   imguiinit.hpp
 * \brief  ImGui初期化ヘッダ
 * 
 * 作成者 keep
 * 作成日 2025/11/27
 * 更新履歴	2025/11/27 村尾颯斗 そんな最近じゃないけど作成
 *			2025/4/27 DirectX12用に実装
 *			2025/5/29 SRVヒープの空きスロットを確保できるように実装
 * *********************************************************************/
#pragma once
#include "imgui.h"
#include "imgui-master/backends/imgui_impl_dx12.h"
#include "imgui-master/backends/imgui_impl_win32.h"
#include "Defines.hpp"
#include <format>

// ---- Define ---- //

/// 表示の切り替え
#define DebugSwitch (true)

// ---------------------------------------------------//
// Shift-JIS文字列をUTF-8に変換してImGuiに表示するマクロ	  //
// ---------------------------------------------------//
#define u8(text) IMGUI::ToUTF8(text).c_str()

class IMGUI
{
	public:
	static bool Start(
		_In_ HWND hWnd,
		_In_ ID3D12Device* device,
		_In_ ID3D12CommandQueue* commandQueue,
		int framecount,
		DXGI_FORMAT rtvformat
		);
	static void BeginFrame();
	static void EndFrame(_In_ ID3D12GraphicsCommandList* commandList);
	static void Release();

	static LRESULT ImGui_WndProHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// Shift-JIS文字列をUTF-8に変換
	//static std::string ToUTF8(const char* sjis);

	static std::string ToUTF8(std::string text);

	static ImVec2 GetMousePosInViewPort();

	static bool IsInitialized() { return s_Initialized; }
private:
	static bool s_Initialized;
};