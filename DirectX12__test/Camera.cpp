#include "Camera.hpp"

Camera::Camera()
{
	m_FovY = DirectX::XMConvertToRadians(60.0f);
	m_Position = { 0.0f, 0.0f, -5.0f };
	m_LookAt = { 0.0f, 0.0f, 0.0f };
	m_Up = { 0.0f, 1.0f, 0.0f };
}

Camera::~Camera()
{
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
