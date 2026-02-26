/*****************************************************************//**
 * \file   Input.hpp
 * \brief  入力管理システム
 *
 * 作成者
 * 作成日 2026/2/26
 * 更新日
 * *********************************************************************/
#pragma once
#include <Windows.h>

 // 前方宣言
class Input;

// キー入力用ヘルパークラス
class KeyState
{
public:
	KeyState(const Input* input, int vkCode) : m_Input(input), m_VKCode(vkCode) {}

	// 押されている間
	bool IsPressed() const;
	operator bool() const { return IsPressed(); }

	// 押された瞬間
	bool Down() const;

	// 離された瞬間
	bool Up() const;

private:
	const Input* m_Input;
	int m_VKCode;
};

// マウスボタン用ヘルパークラス
class MouseButton
{
public:
	MouseButton(const Input* input, int button) : m_Input(input), m_Button(button) {}

	/// @brief マウスボタンが押されているか
	/// @return 押されている場合はtrue、そうでない場合はfalse
	bool IsPressed() const;
	operator bool() const { return IsPressed(); }

	/// @brief マウスボタンが押された瞬間
	/// @return 押された場合はtrue、そうでない場合はfalse
	bool Down() const;

	/// @brief マウスボタンが離された瞬間
	/// @return 離された場合はtrue、そうでない場合はfalse
	bool Up() const;

private:
	const Input* m_Input;
	int m_Button;
};

// マウス情報用クラス
class Mouse
{
public:
	Mouse(const Input* input) : m_Input(input) {}

	/// @brief マウスのX座標を取得
	/// @return X座標
	int X() const;

	/// @brief マウスのY座標を取得
	/// @return Y座標
	int Y() const;

	/// @brief マウスのX座標の変化量を取得
	int DeltaX() const;

	/// @brief マウスのY座標の変化量を取得
	int DeltaY() const;

	/// @brief マウスホイールの回転量を取得
	int Wheel() const;

	/// @brief マウスの左クリックを検出
	/// @return 押されている場合はtrue、そうでない場合はfalse
	MouseButton Left() const { return MouseButton(m_Input, 0); }

	/// @brief マウスの右クリックを検出
	/// @return 押されている場合はtrue、そうでない場合はfalse
	MouseButton Right() const { return MouseButton(m_Input, 1); }

	/// @brief マウスの中クリックを検出
	/// @return 押されている場合はtrue、そうでない場合はfalse
	MouseButton Middle() const { return MouseButton(m_Input, 2); }

private:
	const Input* m_Input;
};

#define INPUT Input::GetInstance()

#include <cstdint>

class Input
{
public:
	enum class MouseButtonType : uint8_t
	{
		Left,
		Right,
		Middle,
		MAX
	};

	/// @brief インスタンスを取得
	/// @return インスタンスへのポインタ
	static Input* GetInstance();

	/// @brief 初期化
	/// @param hWnd 
	void Init(HWND hWnd);

	/// @brief 更新処理
	void Update();

	struct Keys
	{
		const Input* input;

		KeyState W() const { return KeyState(input, 'W'); }
		KeyState A() const { return KeyState(input, 'A'); }
		KeyState S() const { return KeyState(input, 'S'); }
		KeyState D() const { return KeyState(input, 'D'); }
		KeyState Space() const { return KeyState(input, VK_SPACE); }
		KeyState Shift() const { return KeyState(input, VK_SHIFT); }
		KeyState Ctrl() const { return KeyState(input, VK_CONTROL); }
		KeyState Escape() const { return KeyState(input, VK_ESCAPE); }
		KeyState Enter() const { return KeyState(input, VK_RETURN); }

		// 数字キー
		KeyState Num0() const { return KeyState(input, '0'); }
		KeyState Num1() const { return KeyState(input, '1'); }
		KeyState Num2() const { return KeyState(input, '2'); }
		KeyState Num3() const { return KeyState(input, '3'); }
		KeyState Num4() const { return KeyState(input, '4'); }
		KeyState Num5() const { return KeyState(input, '5'); }
		KeyState Num6() const { return KeyState(input, '6'); }
		KeyState Num7() const { return KeyState(input, '7'); }
		KeyState Num8() const { return KeyState(input, '8'); }
		KeyState Num9() const { return KeyState(input, '9'); }

		// 矢印キー
		KeyState Up() const { return KeyState(input, VK_UP); }
		KeyState Down() const { return KeyState(input, VK_DOWN); }
		KeyState Left() const { return KeyState(input, VK_LEFT); }
		KeyState Right() const { return KeyState(input, VK_RIGHT); }

		// カスタムキー
		KeyState Custom(int vkCode) const { return KeyState(input, vkCode); }
	};

	Keys Key;
	Mouse MouseInput;

	bool GetKey(int vkCode) const;
	bool GetKeyDown(int vkCode) const;
	bool GetKeyUp(int vkCode) const;

	bool GetMouseButton(int button) const;
	bool GetMouseButtonDown(int button) const;
	bool GetMouseButtonUp(int button) const;

	int GetMouseX() const { return m_MouseX; }
	int GetMouseY() const { return m_MouseY; }
	int GetMouseDeltaX() const { return m_MouseDeltaX; }
	int GetMouseDeltaY() const { return m_MouseDeltaY; }
	int GetMouseWheel() const { return m_MouseWheel; }

	void ShowCursor(bool show);
	void SetCursorLock(bool lock);

private:
	Input();
	~Input() = default;
	Input(const Input&) = delete;
	void operator=(const Input&) = delete;

	static const int KEY_MAX = 256;
	bool m_Keys[KEY_MAX];
	bool m_PrevKeys[KEY_MAX];

	static const int MOUSE_BUTTON_MAX = 3;
	bool m_MouseButtons[MOUSE_BUTTON_MAX];
	bool m_PrevMouseButtons[MOUSE_BUTTON_MAX];

	int m_MouseX, m_MouseY;
	int m_PrevMouseX, m_PrevMouseY;
	int m_MouseDeltaX, m_MouseDeltaY;
	int m_MouseWheel;

	bool m_CursorLocked;
	HWND m_hWnd;

	friend class KeyState;
	friend class MouseButton;
	friend class Mouse;
	friend LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	void OnMouseWheel(int delta) { m_MouseWheel = delta; }
};