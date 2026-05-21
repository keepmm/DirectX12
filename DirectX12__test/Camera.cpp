#include "Camera.hpp"
#include "Input.hpp"
#include "Time.hpp"
#include "imguiinit.hpp"

void Camera::DebugWindow()
{
	if (ImGui::Begin("Camera"))
	{
		ImGui::Text("Position: (%.2f, %.2f, %.2f)", m_Position.x, m_Position.y, m_Position.z);
		ImGui::Text("LookAt: (%.2f, %.2f, %.2f)", m_LookAt.x, m_LookAt.y, m_LookAt.z);
		ImGui::Text("Up: (%.2f, %.2f, %.2f)", m_Up.x, m_Up.y, m_Up.z);
		ImGui::Text("Rotation: (%.2f, %.2f, %.2f, %.2f)", m_Rotation.x, m_Rotation.y, m_Rotation.z, m_Rotation.w);
	}
	ImGui::End();
}

Camera::Camera()
{
	m_FovY = DirectX::XMConvertToRadians(60.0f);
	m_Position = { 0.0f, 2.0f, -5.0f };
	m_LookAt = { 0.0f, 0.0f, 0.0f };
	m_Up = { 0.0f, 1.0f, 0.0f };

	// forwardを計算
	vector pos = DirectX::XMLoadFloat3(&m_Position);
	vector target = DirectX::XMLoadFloat3(&m_LookAt);
	vector f = DirectX::XMVectorSubtract(pos,target);

	// 正規化
	vector forward = DirectX::XMVector3Normalize(f);

	// 向きを計算
	float3 dir{};
	DirectX::XMStoreFloat3(&dir, forward);

	m_Pitch = std::asinf(dir.y);
	m_Yaw = std::atan2f(dir.x, dir.z);

	// 回転をクォータニオンで表現
	const vector q = DirectX::XMQuaternionRotationRollPitchYaw(m_Pitch, m_Yaw, 0.0f);
	DirectX::XMStoreFloat4(&m_Rotation, DirectX::XMQuaternionNormalize(q));
}

Camera::~Camera()
{
}

void Camera::Update()
{
	const float deltaTime = TIME->GetDeltaTime();

	if (INPUT->MouseInput.Right().IsPressed())
	{
		// マウスの移動量に応じてカメラの回転を更新
		m_Yaw -= static_cast<float>(INPUT->MouseInput.DeltaX()) * m_RotateSpeed;
		m_Pitch -= static_cast<float>(INPUT->MouseInput.DeltaY()) * m_RotateSpeed;

		// ピッチの制限
		m_Pitch = std::clamp(m_Pitch, -DirectX::XM_PIDIV2 + 0.01f, DirectX::XM_PIDIV2 - 0.01f);
	}

	{
		// 回転をクォータニオンで表現
		const vector q = DirectX::XMQuaternionRotationRollPitchYaw(m_Pitch, m_Yaw, 0.0f);
		DirectX::XMStoreFloat4(&m_Rotation, DirectX::XMQuaternionNormalize(q));
	}
	
	// クォータニオンで回転を格納
	const vector rotation = DirectX::XMLoadFloat4(&m_Rotation);

	// カメラのベクトル
	vector moveLocal = DirectX::XMVectorZero();

	if (INPUT->Key.W().IsPressed()) moveLocal = DirectX::XMVectorAdd(moveLocal, DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
	if (INPUT->Key.S().IsPressed()) moveLocal = DirectX::XMVectorAdd(moveLocal, DirectX::XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f));
	if (INPUT->Key.D().IsPressed()) moveLocal = DirectX::XMVectorAdd(moveLocal, DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
	if (INPUT->Key.A().IsPressed()) moveLocal = DirectX::XMVectorAdd(moveLocal, DirectX::XMVectorSet(-1.0f, 0.0f, 0.0f, 0.0f));
	if (INPUT->Key.Space().IsPressed()) moveLocal = DirectX::XMVectorAdd(moveLocal,DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
	if (INPUT->Key.Shift().IsPressed()) moveLocal = DirectX::XMVectorAdd(moveLocal,DirectX::XMVectorSet(0.0f, -1.0f, 0.0f, 0.0f));

	// 移動ベクトルが0じゃない場合は処理する
	if (!DirectX::XMVector3Equal(moveLocal, DirectX::XMVectorZero()))
	{
		// 正規化
		moveLocal = DirectX::XMVector3Normalize(moveLocal);
		
		// ローカル座標をワールド座標に変換
		const vector moveWorld = DirectX::XMVectorSet(
			DirectX::XMVectorGetX(moveLocal) * m_MoveSpeed * deltaTime,
			DirectX::XMVectorGetY(moveLocal) * m_MoveSpeed * deltaTime,
			DirectX::XMVectorGetZ(moveLocal) * m_MoveSpeed * deltaTime,
			0.0f
		);

		// ワールド座標で移動を適用
		vector pos = DirectX::XMLoadFloat3(&m_Position);
		pos = DirectX::XMVectorAdd(pos, DirectX::XMVector3Rotate(moveWorld, rotation));
		DirectX::XMStoreFloat3(&m_Position, pos);
	}

	// LookAt/Upをクォータニオンから更新
	{
		const vector pos = DirectX::XMLoadFloat3(&m_Position);
		const vector forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
		const vector up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

		DirectX::XMStoreFloat3(&m_Up, up);

		float3 lookAt{};
		DirectX::XMStoreFloat3(&lookAt, DirectX::XMVectorAdd(pos,forward));
		m_LookAt = lookAt;
	}
}

float4x4 Camera::GetViewMatrix(bool transpose) const
{
	vector pos = XMLoadFloat3(&m_Position);
	vector eye = XMLoadFloat3(&m_LookAt);
	vector up  = XMLoadFloat3(&m_Up);

	matrix view = DirectX::XMMatrixLookAtLH(pos, eye, up);

	if (transpose) view = DirectX::XMMatrixTranspose(view);

	float4x4 viewMatrix;
	DirectX::XMStoreFloat4x4(&viewMatrix, view);
	return viewMatrix;
}

float4x4 Camera::GetProjectionMatrix(bool transpose) const
{
	float4x4 mat;
	matrix proj = DirectX::XMMatrixPerspectiveFovLH(
		m_FovY,
		(float)WINDOW_WIDTH / (float)WINDOW_HEIGHT,
		m_Near,
		m_Far
	);

	if (transpose) proj = DirectX::XMMatrixTranspose(proj);
	DirectX::XMStoreFloat4x4(&mat, proj);
	return mat;
}
