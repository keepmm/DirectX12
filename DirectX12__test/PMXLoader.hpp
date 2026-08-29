#pragma once
#include "ModelData.hpp"
#include <string>

class PMXLoader
{
public:
	// PMXを読んで ModelCpuData に流し込む
	static bool Parse(const std::string& filepath, float scale, ModelCpuData& out);
};