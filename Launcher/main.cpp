/*****************************************************************//**
 * \file   main.cpp
 * \brief  プロジェクト選択ランチャー(Win32)
 *         選ばれたプロジェクトを -project 付きでエンジン exe に渡して起動する
 *
 * 作成者 keep
 * 作成日 2026/9/10
 * 更新履歴 9/10 作成
 * *********************************************************************/
#include <Windows.h>
#include <CommCtrl.h>
#include <filesystem>
#include <string>
#include <vector>

#include "../DirectX12__test/Project.hpp"

#pragma comment(lib, "Comctl32.lib")

namespace fs = std::filesystem;

namespace
{
	constexpr int ID_LIST     = 1001;
	constexpr int ID_OPEN     = 1002;
	constexpr int ID_BROWSE   = 1003;
	constexpr int ID_NAME     = 1004;
	constexpr int ID_DESTDIR  = 1005;
	constexpr int ID_DESTPICK = 1006;
	constexpr int ID_CREATE   = 1007;

	HWND g_List = nullptr;
	HWND g_Name = nullptr;
	HWND g_Dest = nullptr;

	std::wstring Widen(const std::string& s)
	{
		if (s.empty()) return {};
		const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
		std::wstring w(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
		return w;
	}

	std::string Narrow(const std::wstring& w)
	{
		if (w.empty()) return {};
		const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
			nullptr, 0, nullptr, nullptr);
		std::string s(len, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
			s.data(), len, nullptr, nullptr);
		return s;
	}

	std::wstring GetText(HWND edit)
	{
		const int len = GetWindowTextLengthW(edit);
		if (len <= 0) return {};
		std::wstring s(len, L'\0');
		GetWindowTextW(edit, s.data(), len + 1);
		return s;
	}

	void ShowError(HWND owner, const std::string& msg)
	{
		MessageBoxW(owner, Widen(msg).c_str(), L"Launcher", MB_OK | MB_ICONWARNING);
	}

	/// @brief エンジン exe を -project 付きで起動する
	/// @note ランチャーと同じフォルダに DirectX12__test.exe がある前提
	bool LaunchEngine(HWND owner, const fs::path& projectRoot)
	{
		wchar_t exePath[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exePath, MAX_PATH);
		const fs::path engine = fs::path(exePath).parent_path() / L"DirectX12__test.exe";

		if (!fs::exists(engine))
		{
			ShowError(owner, "エンジン本体が見つかりません: " + engine.string());
			return false;
		}

		std::wstring cmd = L"\"" + engine.wstring() + L"\" -project \""
			+ projectRoot.wstring() + L"\"";

		// CreateProcessW は第2引数を書き換えるので可変バッファを渡す
		std::vector<wchar_t> buf(cmd.begin(), cmd.end());
		buf.push_back(L'\0');

		STARTUPINFOW si{ sizeof(si) };
		PROCESS_INFORMATION pi{};

		// 作業ディレクトリはエンジン側が Project::Activate で切り替えるので指定しない
		if (!CreateProcessW(engine.c_str(), buf.data(), nullptr, nullptr, FALSE,
			0, nullptr, nullptr, &si, &pi))
		{
			ShowError(owner, "エンジンの起動に失敗しました");
			return false;
		}

		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}

	void RefreshList()
	{
		SendMessageW(g_List, LB_RESETCONTENT, 0, 0);

		for (const auto& r : PROJECT->GetRecents())
		{
			// recents は UTF-8。fs::path に直接渡すと日本語が化ける
			const fs::path root = Utf8ToPath(r);
			const bool exists = fs::exists(root);
			std::wstring label = root.filename().wstring()
				+ L"    " + root.wstring();
			if (!exists) label += L"   (見つかりません)";

			SendMessageW(g_List, LB_ADDSTRING, 0,
				reinterpret_cast<LPARAM>(label.c_str()));
		}

		if (!PROJECT->GetRecents().empty())
		{
			SendMessageW(g_List, LB_SETCURSEL, 0, 0);
		}
	}

	/// @brief 指定パスのプロジェクトを開いてエンジンを起動し、ランチャーを閉じる
	void OpenAndLaunch(HWND hWnd, const fs::path& path)
	{
		std::string err;
		if (!PROJECT->Open(path, err))
		{
			ShowError(hWnd, err);
			RefreshList();   // 失敗した行の表示を更新
			return;
		}

		if (LaunchEngine(hWnd, PROJECT->GetRoot()))
		{
			DestroyWindow(hWnd);
		}
	}

	void OnOpenSelected(HWND hWnd)
	{
		const LRESULT sel = SendMessageW(g_List, LB_GETCURSEL, 0, 0);
		if (sel == LB_ERR) return;

		const auto& recents = PROJECT->GetRecents();
		if (sel < 0 || sel >= static_cast<LRESULT>(recents.size())) return;

		OpenAndLaunch(hWnd, Utf8ToPath(recents[static_cast<size_t>(sel)]));
	}

	void OnBrowseOpen(HWND hWnd)
	{
		const std::string dir = PickProjectFolder();	// UTF-8
		if (dir.empty()) return;
		OpenAndLaunch(hWnd, Utf8ToPath(dir));
	}

	void OnCreate(HWND hWnd)
	{
		const std::wstring name = GetText(g_Name);
		const std::wstring dest = GetText(g_Dest);

		if (name.empty() || dest.empty())
		{
			ShowError(hWnd, "プロジェクト名と作成先を入力してください");
			return;
		}

		std::string err;
		if (!PROJECT->Create(fs::path(dest), Narrow(name), err))
		{
			ShowError(hWnd, err);
			return;
		}

		if (LaunchEngine(hWnd, PROJECT->GetRoot()))
		{
			DestroyWindow(hWnd);
		}
	}

	void CreateControls(HWND hWnd)
	{
		auto mk = [hWnd](const wchar_t* cls, const wchar_t* text, DWORD style,
			int x, int y, int w, int h, int id)
			{
				return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
					x, y, w, h, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
					GetModuleHandleW(nullptr), nullptr);
			};

		mk(L"STATIC", L"最近開いたプロジェクト", 0, 12, 12, 300, 20, -1);

		g_List = mk(L"LISTBOX", L"",
			WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY, 12, 36, 600, 260, ID_LIST);

		mk(L"BUTTON", L"開く", WS_TABSTOP, 12, 306, 120, 30, ID_OPEN);
		mk(L"BUTTON", L"フォルダから開く...", WS_TABSTOP, 142, 306, 170, 30, ID_BROWSE);

		mk(L"STATIC", L"新規作成", 0, 12, 352, 200, 20, -1);

		g_Name = mk(L"EDIT", L"MyProject", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
			12, 376, 180, 24, ID_NAME);
		g_Dest = mk(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
			200, 376, 290, 24, ID_DESTDIR);

		mk(L"BUTTON", L"作成先...", WS_TABSTOP, 498, 376, 114, 24, ID_DESTPICK);
		mk(L"BUTTON", L"作成", WS_TABSTOP, 12, 410, 120, 30, ID_CREATE);

		// 既定フォントを当てないと古い見た目になる
		HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
		EnumChildWindows(hWnd, [](HWND child, LPARAM f) -> BOOL
			{
				SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
				return TRUE;
			}, reinterpret_cast<LPARAM>(font));
	}

	LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_CREATE:
			CreateControls(hWnd);
			RefreshList();
			return 0;

		case WM_COMMAND:
		{
			const int id = LOWORD(wParam);
			const int code = HIWORD(wParam);

			// リストのダブルクリックでも開く
			if (id == ID_LIST && code == LBN_DBLCLK) { OnOpenSelected(hWnd); return 0; }

			if (code != BN_CLICKED) break;

			switch (id)
			{
			case ID_OPEN:   OnOpenSelected(hWnd); return 0;
			case ID_BROWSE: OnBrowseOpen(hWnd);   return 0;
			case ID_CREATE: OnCreate(hWnd);       return 0;

			case ID_DESTPICK:
			{
				const std::string dir = PickProjectFolder();
				if (!dir.empty())
				{
					SetWindowTextW(g_Dest, Widen(dir).c_str());
				}
				return 0;
			}
			default: break;
			}
			break;
		}

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		default:
			break;
		}

		return DefWindowProcW(hWnd, msg, wParam, lParam);
	}
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
	// PickProjectFolder(IFileOpenDialog) が COM を使う
	const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
	{
		return 0;
	}

	INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&icc);

	PROJECT->LoadRecents();

	WNDCLASSEXW wc{ sizeof(wc) };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
	wc.lpszClassName = L"DX12EngineLauncher";
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

	if (RegisterClassExW(&wc) == 0)
	{
		CoUninitialize();
		return 0;
	}

	// クライアント領域が 624x452 になるように外枠を足す
	RECT rc{ 0, 0, 624, 452 };
	const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect(&rc, style, FALSE);

	HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"DirectX12 Engine - プロジェクト",
		style, CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hInstance, nullptr);

	if (hWnd == nullptr)
	{
		CoUninitialize();
		return 0;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	MSG msg{};
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		if (!IsDialogMessageW(hWnd, &msg))   // Tab移動を効かせる
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	CoUninitialize();
	return 0;
}
