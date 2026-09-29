/*****************************************************************//**
 * ile   PadLook.cpp
 * rief  ゲームパッドの右スティックで視点を回す
 *
 * 作成者 keepmm
 * 作成日 2026/9/20
 * 更新履歴 	9/20 作成
 * *********************************************************************/
#include "MonoBehavior.hpp"
#include "Components.hpp"
#include "RegisterScript.hpp"
#include "Input.hpp"

#include <algorithm>

/// @brief 右スティックで FollowCamera の視点を回す
/// @note FollowCameraComponent が付いている Entity(カメラ)に付ける。
///       マウス操作は FollowCameraSystem 側がそのまま見ているので、
///       このスクリプトはパッドぶんを足すだけでよい
class PadLook : public MonoBehavior
{
public:
	// パッド感度(ラジアン/秒)。マウス感度とは単位が違うので別に持つ
	SERIALIZE_FIELD_RANGE(float, rotateSpeed, 0.1f, 10.0f, 2.5f);

	// 右スティックの上下を入れ替える
	SERIALIZE_FIELD(bool, invertY, false);

	void OnStart() override
	{
		if (!HasComponent<FollowCameraComponent>())
		{
			OutputDebugStringA(
				"[PadLook] FollowCameraComponent が無い Entity に付いています\n");
		}
	}

	void OnUpdate(float deltatime) override
	{
		if (!HasComponent<FollowCameraComponent>()) return;

		auto& fc = GetComponent<FollowCameraComponent>();
		if (!fc.enabled) return;

		const float2 look = INPUT->GetVector("LookX", "LookY");
		if (look.x == 0.0f && look.y == 0.0f) return;

		// マウスは「動いたピクセル数 × 感度」だが、スティックは -1〜1 の倒し具合。
		// deltatime を掛けないとフレームレートで速さが変わる
		fc.yaw += look.x * rotateSpeed * deltatime;

		// スティックは上が + 、pitch は下を向くと + なので既定で反転する
		const float pitchInput = invertY ? look.y : -look.y;
		fc.pitch += pitchInput * rotateSpeed * deltatime;

		// FollowCameraSystem の clamp はマウス操作中にしか走らないため、
		// パッドだけで回したときのために自分でも止める
		fc.pitch = std::clamp(fc.pitch,
			DirectX::XMConvertToRadians(fc.minPitch),
			DirectX::XMConvertToRadians(fc.maxPitch));
	}
};

REGISTER_SCRIPT(PadLook)
