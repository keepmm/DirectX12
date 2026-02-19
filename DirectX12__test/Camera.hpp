/*****************************************************************//**
 * \file   Camera.hpp
 * \brief  カメラの基底クラス
 * 
 * 作成者 
 * 作成日 2026/2/17
 * 更新履歴
 * *********************************************************************/
#pragma once

#include "Defines.hpp"

class Camera
{
public:
	Camera();
	~Camera();

	float4x4 GetViewMatrix(bool transpose = true) const;
	float4x4 GetProjectionMatrix(bool transpose = true) const;

private:
	float3 m_Position{};		// カメラの座標
	float3 m_LookAt{};		// カメラの注視点
	float3 m_Up{};			// カメラの上方向
	float  m_FovY = 60.0f;	// カメラの垂直方向の視野角4
	float m_Near = 0.1f;	// カメラの近距離クリップ面
	float m_Far = 100.0f;	// カメラの遠距離クリップ面
};

