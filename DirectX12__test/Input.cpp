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

bool KeyState::IsPressed() const
{
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

	// 過去のマウス座標を保存
	m_PrevMouseX = m_MouseX;
	m_PrevMouseY = m_MouseY;

	// 新しいマウス座標を取得
	m_MouseX = pt.x;
	m_MouseY = pt.y;

	// マウスの移動量計算
	m_MouseDeltaX = m_MouseX - m_PrevMouseX;
	m_MouseDeltaY = m_MouseY - m_PrevMouseY;

	// カーソルロック処理
	if (m_CursorLocked)
	{
		RECT rect;
		GetClientRect(m_hWnd, &rect);
		POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };
		ClientToScreen(m_hWnd, &center);
		SetCursorPos(center.x, center.y);
	}

	// ホイールはフレーム終了時にリセット
	m_MouseWheel = 0;
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
	if (!lock)
	{
		// ロック解除時にカーソルを現在のマウス位置に移動
		POINT pt;
		GetCursorPos(&pt);
		ScreenToClient(m_hWnd, &pt);
		SetCursorPos(pt.x, pt.y);
	}
}
