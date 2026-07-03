/*****************************************************************//**
 * \file   ModelLoader.hpp
 * \brief  assimp使いたい
 * 
 * 作成者 keeeep
 * 作成日 2026/5/4
 * 更新履歴	5.04 作成
 * 　　　　	6.28 非同期ロード実装
 * *********************************************************************/
#pragma once

#include "ModelData.hpp"

class Mesh;

class ModelLoader
{
public:
	static ModelLoadResult LoadFromFile(
		_In_ const ComPtr<ID3D12Device>& device,
		_In_ const std::string& filepath,
		float scale = 1.0f);

	static ModelCpuData ParseFile(
		_In_ const std::string& filepath,
		_In_ float scale= 1.0f
	);

	static ModelLoadResult upload(
		_In_ const ModelCpuData& cpu
	);
};

