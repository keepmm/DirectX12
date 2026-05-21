/*****************************************************************//**
 * \file   ModelLoader.hpp
 * \brief  assimpg‚¢‚½‚¢
 * 
 * ì¬Ò keeeep
 * ì¬“ú 2026/5/4
 * XV—š—ğ
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
};

