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
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "EngineAPI.hpp"
#include "Defines.hpp"

 // 前方宣言
class Input;

// キー入力用ヘルパークラス
class ENGINE_API KeyState
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
class ENGINE_API MouseButton
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
class ENGINE_API Mouse
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

namespace PadButton
{
	enum : std::uint16_t
	{
		DPadUp		= 0x0001,
		DPadDown	= 0x0002,
		DPadLeft	= 0x0004,
		DPadRight	= 0x0008,
		Start		= 0x0010,
		Back		= 0x0020,
		LStick		= 0x0040,
		RStick		= 0x0080,
		LShoulder	= 0x0100,
		RShoulder	= 0x0200,
		LTrigger	= 0x0400,	// XInput 未使用ビットを流用
		RTrigger	= 0x0800,	// 同上
		A			= 0x1000,
		B			= 0x2000,
		X			= 0x4000,
		Y			= 0x8000,
		None		= 0x0000,
	};
};

/// @brief ゲームパッドのアナログ軸
enum class PadAxis : std::uint8_t
{
	LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger,
	MAX,
	None = 0xFF,
};

#define INPUT Input::GetInstance()

class ENGINE_API Input
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


	/// @brief ゲームパッド一台ぶんの状態を読む窓口
	struct Pad
	{
		const Input* input;
		int index;

		/// @brief 接続されているか
		/// @return されている場合 true
		bool Connected() const;

		/// @brief 押されているか
		/// @return 押されている間 true
		bool IsPressed(std::uint16_t button)const;

		/// @brief 押した瞬間
		/// @return 押した瞬間 true
		bool Down(std::uint16_t button) const;

		/// @brief 離した瞬間
		bool Up(std::uint16_t button) const;

		/// @brief アナログ軸の値
		/// @return -1.0 ~ 1.0 トリガーは0.0 ~ 1.0
		float Axis(PadAxis axis)const;

		/// @brief 左スティックを2軸まとめて
		float2 LeftStick()  const { return float2(Axis(PadAxis::LeftX), Axis(PadAxis::LeftY)); }
		float2 RightStick() const { return float2(Axis(PadAxis::RightX), Axis(PadAxis::RightY)); }

		/// @brief 振動させる
		/// @param left  低周波モーター(重い揺れ) 0.0〜1.0
		/// @param right 高周波モーター(細かい揺れ) 0.0〜1.0
		/// @param seconds この秒数だけ振らせる。0 なら止めるまで振り続ける
		void SetVibration(float left, float right, float seconds = 0.0f) const;
	};

	/// @brief ゲームパッドを取得する
	/// @param index 0 ~ 3。省略時は 0
	Pad Pad0(int index = 0) const { return { this,index }; }

	struct ActionBinding
	{
		std::vector<int>           keys;		// 仮想キーコード(ボタン用)
		std::vector<std::uint16_t> padButtons;	// PadButton::* (ボタン用)

		int      negKey = 0;			// 軸のマイナス側キー(例: 'A')
		int      posKey = 0;			// 軸のプラス側キー (例: 'D')
		PadAxis  padAxis = PadAxis::None;	// 軸に割り当てるパッドの軸
		bool     invert = false;		// 軸の符号を反転する(Y軸の上下が逆のとき)
	};

	void BindButton(
		_In_ const std::string& name,
		_In_ int vkCode,
		_In_ std::uint16_t padButton = PadButton::None
	);

	/// @brief 軸のアクションを登録する
	/// @param negKey マイナス方向のキー / @param posKey プラス方向のキー
	/// @param invert パッドのY軸のように符号が逆のとき true
	void BindAxis(const std::string& name, int negKey, int posKey,
		PadAxis padAxis = PadAxis::None, bool invert = false);

	/// @brief 登録済みのバインドを全部消す
	void ClearBinding();

	bool GetButton(const std::string& name) const;		// 押されている間
	bool GetButtonDown(const std::string& name) const;	// 押した瞬間
	bool GetButtonUp(const std::string& name) const;	// 離した瞬間

	/// @brief 軸の値を取る
	/// @return -1.0 ~ 1.0。キーとパッドの両方が動いていたら絶対値の大きいほう
	float GetAxis(const std::string& name)const;

	/// @brief 2軸をまとめて取り、長さ1を超えないよう丸める
	float2 GetVector(const std::string& xName, const std::string& yName)const;

	/// @brief 既定のバインド(WASD移動 / Space ジャンプなど)を登録する
	void SetupDefaultActions();

	/// @brief どのパッドを見るか。-1 なら接続されている全部の OR を取る
	void SetActivePad(int index) { m_ActivePad = index; }
	int  GetActivePad() const { return m_ActivePad; }

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

	void SetImGuiCapture(bool keyboard, bool mouse, bool text)
	{
		m_ImGuiWantKeyboard = keyboard;
		m_ImGuiWantMouse = mouse;
		m_ImGuiWantText = text;
	}

	bool IsKeyboardCaptured() const { return  m_ImGuiWantText; }
	bool IsMouseCaptured() const { return m_ImGuiWantMouse; }

	void SetViewportHovered(bool v) { m_ViewportHovered = v; }
	bool IsViewportHovered() const { return m_ViewportHovered; }
private:
	bool m_ViewportHovered = false;
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

	// ---- ゲームパッド ----
	static const int PAD_MAX = 4;
	static const int AXIS_MAX = static_cast<int>(PadAxis::MAX);

	bool          m_PadConnected[PAD_MAX]{};
	std::uint16_t m_PadButtons[PAD_MAX]{};		// 今フレームの押下ビット
	std::uint16_t m_PrevPadButtons[PAD_MAX]{};	// 前フレーム
	float         m_PadAxis[PAD_MAX][AXIS_MAX]{};
	float         m_VibrationTimer[PAD_MAX]{};	// 残り振動時間。0 以下で停止させる

	/// @brief 未接続パッドへの XInputGetState は重いので、間引くためのタイマー
	float m_PadScanTimer = 0.0f;

	int m_ActivePad = -1;	// -1 = 全パッドの OR

	// ---- アクションマップ ----
	// dllexport したクラスが STL をメンバに持つと C4251 が出る。
	// Input は exe 側にしか実体が無く、DLL(Scripts) からは関数越しにしか触らないため無害
#pragma warning(push)
#pragma warning(disable: 4251)
	std::unordered_map<std::string, ActionBinding> m_Actions;
#pragma warning(pop)

	/// @brief 全パッドの状態を取り込む(Update から呼ばれる)
	void UpdatePads();

	/// @brief アクションを引く(無ければ nullptr)
	const ActionBinding* FindAction(const std::string& name) const;

	/// @brief 全パッド(または m_ActivePad)のボタンビットをまとめる
	std::uint16_t PadButtonMask(bool prev) const;

	/// @brief 全パッド(または m_ActivePad)のうち、絶対値が最大の軸値
	float PadAxisValue(PadAxis axis) const;

	friend struct Pad;

	friend class KeyState;
	friend class MouseButton;
	friend class Mouse;
	friend LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	void OnMouseWheel(int delta) { m_MouseWheel = delta; }

	bool m_ImGuiWantKeyboard = false;
	bool m_ImGuiWantMouse = false;
	bool m_ImGuiWantText = false;
};