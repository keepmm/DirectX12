/*****************************************************************//**
 * \file   Camera.hpp
 * \brief  カメラの基底クラス
 * 
 * 作成者 
 * 作成日 2026/2/17
 * 更新履歴	2.17 作成
 *			4.28 update関数追加
 * *********************************************************************/
#pragma once

#include "Defines.hpp"

class Camera
{
public:
	Camera();
	~Camera();

	void Update();

	void DebugWindow();

	float4x4 GetViewMatrix(bool transpose = true) const;
	float4x4 GetProjectionMatrix(bool transpose = true) const;

private:
	float3 m_Position{};		// カメラの座標
	float3 m_LookAt{};			// カメラの注視点
	float3 m_Up{};				// カメラの上方向
	float4 m_Rotation{ 0.0f,0.0f,0.0f,1.0f };

	float  m_FovY = 60.0f;		// カメラの垂直方向の視野角4
	float m_Near = 0.1f;		// カメラの近距離クリップ面
	float m_Far = 100.0f;		// カメラの遠距離クリップ面

	float m_MoveSpeed = 0.5f;
	float m_RotateSpeed = 0.0025f;
	float m_Yaw = 0.0f;
	float m_Pitch = 0.0f;
};

