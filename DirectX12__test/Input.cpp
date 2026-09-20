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

#include <Xinput.h>
#include <algorithm>
#include <cmath>
#include "Time.hpp"
#include "Logger.hpp"

#pragma comment(lib, "Xinput.lib")

#undef max
#undef min

namespace
{
	// スティックのデッドゾーン(XInput の推奨値を 0〜1 に正規化したもの)
	constexpr float kStickDeadZone = 7849.0f / 32767.0f;	// 左スティック相当
	constexpr float kTriggerDeadZone = 30.0f / 255.0f;
	constexpr float kTriggerButtonThreshold = 0.5f;	// これを超えたらトリガーをボタン扱い
	constexpr float kPadScanInterval = 1.0f;		// 未接続パッドを探し直す間隔(秒)

	/// @brief デッドゾーンを抜いて -1〜1 に均す
	/// @note 単純に切り捨てると境界で値が飛ぶので、残りの範囲を引き伸ばして繋ぐ
	float ApplyDeadZone(float value, float deadZone)
	{
		const float mag = std::fabs(value);
		if (mag <= deadZone) return 0.0f;

		const float scaled = (mag - deadZone) / (1.0f - deadZone);
		return (value < 0.0f) ? -scaled : scaled;
	}

	float NormalizeStick(SHORT raw)
	{
		// -32768 側が 1 だけ範囲が広いので切り上げて対称にする
		return std::max(-1.0f, static_cast<float>(raw) / 32767.0f);
	}
}

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

bool Input::Pad::Connected() const
{
	if (index < 0 || index >= PAD_MAX) return false;
	return input->m_PadConnected[index];
}

bool Input::Pad::IsPressed(std::uint16_t button) const
{
	if (!Connected()) return false;
	return (input->m_PadButtons[index] & button) != 0;
}

bool Input::Pad::Down(std::uint16_t button) const
{
	if (!Connected()) return false;
	return  (input->m_PadButtons[index] & button) != 0
		&& (input->m_PrevPadButtons[index] & button) == 0;
}

bool Input::Pad::Up(std::uint16_t button) const
{
	if (!Connected()) return false;
	return  (input->m_PadButtons[index] & button) == 0
		&& (input->m_PrevPadButtons[index] & button) != 0;
}

float Input::Pad::Axis(PadAxis axis) const
{
	if (!Connected() || axis == PadAxis::None) return 0.0f;
	return input->m_PadAxis[index][static_cast<int>(axis)];
}

void Input::Pad::SetVibration(float left, float right, float seconds) const
{
	if (!Connected()) return;

	XINPUT_VIBRATION vib{};
	vib.wLeftMotorSpeed = static_cast<WORD>(std::clamp(left, 0.0f, 1.0f) * 65535.0f);
	vib.wRightMotorSpeed = static_cast<WORD>(std::clamp(right, 0.0f, 1.0f) * 65535.0f);
	XInputSetState(index, &vib);

	// const 関数から書きたいので const_cast。Pad は値渡しの一時オブジェクトで、
	// 実体は Input 側にあるため論理的な constness は壊れていない
	const_cast<Input*>(input)->m_VibrationTimer[index] = (seconds > 0.0f) ? seconds : 0.0f;
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

	SetupDefaultActions();
}

/// @brief 全パッドの状態を取り込む
/// @note 未接続の index への XInputGetState は数百μs かかることがあるため、
///       落ちているパッドは kPadScanInterval おきにしか探し直さない
void Input::UpdatePads()
{
	const float dt = TIME->GetDeltaTime();

	m_PadScanTimer -= dt;
	const bool rescan = (m_PadScanTimer <= 0.0f);
	if (rescan) m_PadScanTimer = kPadScanInterval;

	for (int i = 0; i < PAD_MAX; ++i)
	{
		m_PrevPadButtons[i] = m_PadButtons[i];

		// 落ちているパッドは毎フレーム叩かない(未接続への問い合わせが重いため)
		if (!m_PadConnected[i] && !rescan)
		{
			m_PadButtons[i] = 0;
			continue;
		}

		const bool wasConnected = m_PadConnected[i];

		XINPUT_STATE state{};
		if (XInputGetState(i, &state) != ERROR_SUCCESS)
		{
			m_PadConnected[i] = false;
			m_PadButtons[i] = 0;
			for (int a = 0; a < AXIS_MAX; ++a) m_PadAxis[i][a] = 0.0f;

			// 繋がってたものが落ちたときだけ出す
			if (wasConnected)
			{
				LOG->LogWarning("GamePad " + std::to_string(i) + "を切断しました");

				// 振動しっぱなしで抜かれたときのために止めておく
				m_VibrationTimer[i] = 0.0f;
			}
			continue;
		}

		m_PadConnected[i] = true;
		if (!wasConnected)
		{
			LOG->LogInfo("Gamepad " + std::to_string(i) + " を接続しました");
		}

		const XINPUT_GAMEPAD& gp = state.Gamepad;

		m_PadAxis[i][(int)PadAxis::LeftX] = ApplyDeadZone(NormalizeStick(gp.sThumbLX), kStickDeadZone);
		m_PadAxis[i][(int)PadAxis::LeftY] = ApplyDeadZone(NormalizeStick(gp.sThumbLY), kStickDeadZone);
		m_PadAxis[i][(int)PadAxis::RightX] = ApplyDeadZone(NormalizeStick(gp.sThumbRX), kStickDeadZone);
		m_PadAxis[i][(int)PadAxis::RightY] = ApplyDeadZone(NormalizeStick(gp.sThumbRY), kStickDeadZone);

		const float lt = ApplyDeadZone(gp.bLeftTrigger / 255.0f, kTriggerDeadZone);
		const float rt = ApplyDeadZone(gp.bRightTrigger / 255.0f, kTriggerDeadZone);
		m_PadAxis[i][(int)PadAxis::LeftTrigger] = lt;
		m_PadAxis[i][(int)PadAxis::RightTrigger] = rt;

		// XInput のビットをそのまま使い、トリガーぶんだけ自前ビットを足す
		std::uint16_t bits = gp.wButtons;
		if (lt > kTriggerButtonThreshold) bits |= PadButton::LTrigger;
		if (rt > kTriggerButtonThreshold) bits |= PadButton::RTrigger;
		m_PadButtons[i] = bits;

		// 振動の時間切れ
		if (m_VibrationTimer[i] > 0.0f)
		{
			m_VibrationTimer[i] -= dt;
			if (m_VibrationTimer[i] <= 0.0f)
			{
				m_VibrationTimer[i] = 0.0f;
				XINPUT_VIBRATION stop{};
				XInputSetState(i, &stop);
			}
		}
	}
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

	UpdatePads();
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

// ====================================================================== //
//                          アクションマップ                              //
// ====================================================================== //

void Input::BindButton(const std::string& name, int vkCode, std::uint16_t padButton)
{
	ActionBinding& b = m_Actions[name];
	if (vkCode != 0)                  b.keys.push_back(vkCode);
	if (padButton != PadButton::None) b.padButtons.push_back(padButton);
}

void Input::BindAxis(const std::string& name, int negKey, int posKey,
	PadAxis padAxis, bool invert)
{
	ActionBinding& b = m_Actions[name];
	b.negKey = negKey;
	b.posKey = posKey;
	b.padAxis = padAxis;
	b.invert = invert;
}

void Input::ClearBinding()
{
	m_Actions.clear();
}

const Input::ActionBinding* Input::FindAction(const std::string& name) const
{
	auto it = m_Actions.find(name);
	return (it == m_Actions.end()) ? nullptr : &it->second;
}

std::uint16_t Input::PadButtonMask(bool prev) const
{
	const auto& src = prev ? m_PrevPadButtons : m_PadButtons;

	if (m_ActivePad >= 0 && m_ActivePad < PAD_MAX)
		return m_PadConnected[m_ActivePad] ? src[m_ActivePad] : 0;

	std::uint16_t bits = 0;
	for (int i = 0; i < PAD_MAX; ++i)
		if (m_PadConnected[i]) bits |= src[i];
	return bits;
}

float Input::PadAxisValue(PadAxis axis) const
{
	if (axis == PadAxis::None) return 0.0f;
	const int a = static_cast<int>(axis);

	if (m_ActivePad >= 0 && m_ActivePad < PAD_MAX)
		return m_PadConnected[m_ActivePad] ? m_PadAxis[m_ActivePad][a] : 0.0f;

	float best = 0.0f;
	for (int i = 0; i < PAD_MAX; ++i)
	{
		if (!m_PadConnected[i]) continue;
		if (std::fabs(m_PadAxis[i][a]) > std::fabs(best)) best = m_PadAxis[i][a];
	}
	return best;
}

bool Input::GetButton(const std::string& name) const
{
	const ActionBinding* b = FindAction(name);
	if (!b) return false;

	// ImGui がキーボードを持っているときはキー側だけ黙らせる(KeyState と同じ扱い)。
	// パッドはエディタに取られないのでそのまま通す
	if (!IsKeyboardCaptured())
		for (int vk : b->keys)
			if (GetKey(vk)) return true;

	const std::uint16_t bits = PadButtonMask(false);
	for (std::uint16_t pb : b->padButtons)
		if (bits & pb) return true;

	return false;
}

bool Input::GetButtonDown(const std::string& name) const
{
	const ActionBinding* b = FindAction(name);
	if (!b) return false;

	if (!IsKeyboardCaptured())
		for (int vk : b->keys)
			if (GetKeyDown(vk)) return true;

	const std::uint16_t now = PadButtonMask(false);
	const std::uint16_t prev = PadButtonMask(true);
	for (std::uint16_t pb : b->padButtons)
		if ((now & pb) && !(prev & pb)) return true;

	return false;
}

bool Input::GetButtonUp(const std::string& name) const
{
	const ActionBinding* b = FindAction(name);
	if (!b) return false;

	if (!IsKeyboardCaptured())
		for (int vk : b->keys)
			if (GetKeyUp(vk)) return true;

	const std::uint16_t now = PadButtonMask(false);
	const std::uint16_t prev = PadButtonMask(true);
	for (std::uint16_t pb : b->padButtons)
		if (!(now & pb) && (prev & pb)) return true;

	return false;
}

float Input::GetAxis(const std::string& name) const
{
	const ActionBinding* b = FindAction(name);
	if (!b) return 0.0f;

	float keyValue = 0.0f;
	if (!IsKeyboardCaptured())
	{
		if (b->negKey && GetKey(b->negKey)) keyValue -= 1.0f;
		if (b->posKey && GetKey(b->posKey)) keyValue += 1.0f;
	}

	const float padValue = PadAxisValue(b->padAxis);

	// キーとパッドを足すと両方入力したとき 2 になるので、大きいほうを採る
	float value = (std::fabs(padValue) > std::fabs(keyValue)) ? padValue : keyValue;
	if (b->invert) value = -value;
	return value;
}

float2 Input::GetVector(const std::string& xName, const std::string& yName) const
{
	float x = GetAxis(xName);
	float y = GetAxis(yName);

	// キーボードの斜め入力は長さ √2 になるので、1 を超えるときだけ正規化する
	const float lenSq = x * x + y * y;
	if (lenSq > 1.0f)
	{
		const float inv = 1.0f / std::sqrt(lenSq);
		x *= inv;
		y *= inv;
	}
	return float2(x, y);
}

void Input::SetupDefaultActions()
{
	ClearBinding();

	// 移動: WASD / 左スティック
	BindAxis("MoveX", 'A', 'D', PadAxis::LeftX);
	BindAxis("MoveY", 'S', 'W', PadAxis::LeftY);

	// 視点: 矢印キー / 右スティック
	BindAxis("LookX", VK_LEFT, VK_RIGHT, PadAxis::RightX);
	BindAxis("LookY", VK_DOWN, VK_UP, PadAxis::RightY);

	BindButton("Jump", VK_SPACE, PadButton::A);
	BindButton("Fire", 0, PadButton::RTrigger);	// キーはマウス左で見るので未割当
	BindButton("Sprint", VK_SHIFT, PadButton::LStick);
	BindButton("Interact", 'E', PadButton::X);
	BindButton("Pause", VK_ESCAPE, PadButton::Start);
	BindButton("Submit", VK_RETURN, PadButton::A);
	BindButton("Cancel", VK_BACK, PadButton::B);
}
