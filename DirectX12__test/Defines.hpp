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
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;
using float2 = DirectX::XMFLOAT2;
using matrix = DirectX::XMMATRIX;
using vector = DirectX::XMVECTOR;
using float4x4 = DirectX::XMFLOAT4X4;

#include <tchar.h>
#define CLASS_NAME _T("DX12Test")
#define PROC_NAME _T("DX12Test")

#define WINDOW_WIDTH 720
#define WINDOW_HEIGHT 480

#define FPS 60
#define FRAME_TIME (1000 / FPS)

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
