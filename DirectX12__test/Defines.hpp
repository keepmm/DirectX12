/*****************************************************************//**
 * \file   Defines.hpp
 * \brief  定義ファイル
 * 
 * 作成者 
 * 作成日 2026/2/17
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <DirectXMath.h>

 // -------------------------------//
 //			 型エイリアス			   //
 // -------------------------------//
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;
using float2 = DirectX::XMFLOAT2;
using matrix = DirectX::XMMATRIX;
using vector = DirectX::XMVECTOR;
using float4x4 = DirectX::XMFLOAT4X4;

using COLOR = DirectX::XMFLOAT4;
using POSITION = DirectX::XMFLOAT3;
using UV = DirectX::XMFLOAT2;
using QUATERNION = DirectX::XMFLOAT4;
using SCALE = DirectX::XMFLOAT3;

// -------------------------------//
//		  演算子オーバーロード		  //
// -------------------------------//

// float3 + float3 の演算子オーバーロード
inline float3 operator+(const float3& a, const float3& b)
{
	return float3(a.x + b.x, a.y + b.y, a.z + b.z);
}

inline float3 operator-(const float3& a, const float3& b)
{
	return float3(a.x - b.x, a.y - b.y, a.z - b.z);
}

inline float3 operator*(const float3& a, const float3& b)
{
	return float3(a.x * b.x, a.y * b.y, a.z * b.z);
}

inline float3 operator*(const float3& a,float b)
{
	return float3(a.x * b, a.y * b, a.z *b);
}

inline vector operator+(const vector& a, const vector& b)
{
	return DirectX::XMVectorAdd(a, b);
}

inline vector operator-(const vector& a, const vector& b)
{
	return DirectX::XMVectorSubtract(a, b);
}

inline vector operator*(const vector& a, const vector& b)
{
	return DirectX::XMVectorMultiply
	(a, b);
}

inline vector operator*(const vector& a, const float& b)
{
	return DirectX::XMVectorScale(a, b);
}

#include <tchar.h>
#define CLASS_NAME _T("DX12Test")
#define PROC_NAME _T("DX12Test")

#define WINDOW_WIDTH 1920
#define WINDOW_HEIGHT 1080

#define FPS 60
#define FRAME_TIME (1000 / FPS)

#include "pch.h"

#include <wrl/client.h>
template<typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

#include <memory>
template<typename T, typename... Args>
std::unique_ptr<T> MakeUnique(Args&&... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
std::shared_ptr<T> MakeShared(Args&&... args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
}
