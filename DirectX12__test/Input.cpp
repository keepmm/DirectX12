/*****************************************************************//**
 * \file   Input.cpp
 * \brief  入力管理システムの実装
 * 
 * 作成者 keepmm
 * 作成日 2026/2/26
 * 更新履歴 	2/26 作成
 * *********************************************************************/
#include "Input.hpp"
#include <cstring>
#include "imguiinit.hpp"

bool KeyState::IsPressed() const
{
	if (m_Input->IsKeyboardCaptured()) return false;
	return m_Input->GetKey(m_VKCode);
}

bool KeyState::Down() const
{
	return m_Input->GetKeyDown(m_VKCode);
}

bool KeyState::Up() const
{
	return m_Input->GetKeyUp(m_VKCode);
}

bool MouseButton::IsPressed() const
{
	return m_Input->GetMouseButton(m_Button);
}

bool MouseButton::Down() const
{
	return m_Input->GetMouseButtonDown(m_Button);
}

bool MouseButton::Up() const
{
	return m_Input->GetMouseButtonUp(m_Button);
}

int Mouse::X() const
{
	return m_Input->GetMouseX();
}

int Mouse::Y() const
{
	return m_Input->GetMouseY();
}

int Mouse::DeltaX() const
{
	return m_Input->GetMouseDeltaX();
}

int Mouse::DeltaY() const
{
	return m_Input->GetMouseDeltaY();
}

int Mouse::Wheel() const
{
	return m_Input->GetMouseWheel();
}

Input::Input() : 
	m_MouseX(0),
	m_MouseY(0),
	m_PrevMouseButtons{ false },
	m_MouseButtons{ false },
	m_PrevKeys{ false },
	m_Keys{ false },
	m_MouseDeltaX(0),
	m_MouseDeltaY(0),
	m_MouseWheel(0),
	m_CursorLocked(false),
	m_hWnd(nullptr),
	Key{ this },
	MouseInput{ this }
{}

Input* Input::GetInstance()
{
	static Input instance;
	return &instance;
}

void Input::Init(HWND hWnd)
{
	m_hWnd = hWnd;

	// マウス位置初期化
	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(m_hWnd, &pt);
	m_MouseX = m_PrevMouseX = pt.x;
	m_MouseY = m_PrevMouseY = pt.y;
}

void Input::Update()
{
	// 前フレームの状態を保存
	memcpy(m_PrevKeys, m_Keys, sizeof(m_Keys));
	memcpy(m_PrevMouseButtons, m_MouseButtons, sizeof(m_MouseButtons));

	for (int i = 0; i < KEY_MAX; ++i)
	{
		m_Keys[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
	}

	// マウスボタン状態取得
	m_MouseButtons[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	m_MouseButtons[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	m_MouseButtons[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

	POINT pt;
	GetCursorPos(&pt);
	ScreenToClient(m_hWnd, &pt);

	RECT rect;
	GetClientRect(m_hWnd, &rect);
	POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };

	if (m_CursorLocked)
	{
		// ロック中：中心からのズレ＝マウス移動量
		m_MouseDeltaX = pt.x - center.x;
		m_MouseDeltaY = pt.y - center.y;

		// カーソルを中心へ戻す（画面上は固定＝動かない）
		POINT sc = center;
		ClientToScreen(m_hWnd, &sc);
		SetCursorPos(sc.x, sc.y);

		// 絶対位置は中心に固定
		m_MouseX = m_PrevMouseX = center.x;
		m_MouseY = m_PrevMouseY = center.y;
	}
	else
	{
		// 通常：前回との差分
		m_PrevMouseX = m_MouseX;
		m_PrevMouseY = m_MouseY;

		if (IMGUI::IsInitialized())
		{
			const ImVec2 p = IMGUI::GetMousePosInViewPort();
			m_MouseX = (int)p.x;
			m_MouseY = (int)p.y;
		}
		else
		{
			m_MouseX = pt.x;
			m_MouseY = pt.y;
		}
		m_MouseDeltaX = m_MouseX - m_PrevMouseX;
		m_MouseDeltaY = m_MouseY - m_PrevMouseY;
	}

	m_MouseWheel = 0;   // ホイールリセット
}

bool Input::GetKey(int vkCode) const
{
	if (vkCode < 0 || vkCode >= KEY_MAX) return false;
	return m_Keys[vkCode];
}

bool Input::GetKeyDown(int vkCode) const
{
	if (vkCode < 0 || vkCode >= KEY_MAX) return false;
	return m_Keys[vkCode] && !m_PrevKeys[vkCode];
}

bool Input::GetKeyUp(int vkCode) const
{
	if (vkCode < 0 || vkCode >= KEY_MAX) return false;
	return !m_Keys[vkCode] && m_PrevKeys[vkCode];
}

bool Input::GetMouseButton(int button) const
{
	if (button < 0 || button >= static_cast<int>(MouseButtonType::MAX)) return false;
	return m_MouseButtons[button];
}

bool Input::GetMouseButtonDown(int button) const
{
	if (button < 0 || button >= static_cast<int>(MouseButtonType::MAX)) return false;
	return m_MouseButtons[button] && !m_PrevMouseButtons[button];
}

bool Input::GetMouseButtonUp(int button) const
{
	if (button < 0 || button >= static_cast<int>(MouseButtonType::MAX)) return false;
	return !m_MouseButtons[button] && m_PrevMouseButtons[button];
}

void Input::ShowCursor(bool show)
{
	::ShowCursor(show);
}

void Input::SetCursorLock(bool lock)
{
	m_CursorLocked = lock;

	RECT rect;
	GetClientRect(m_hWnd, &rect);
	POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
	ClientToScreen(m_hWnd, &center);
	SetCursorPos(center.x, center.y);   // ロック/アンロック両方で中心へ
}
