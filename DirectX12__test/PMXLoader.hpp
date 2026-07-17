#pragma once
#include "ModelData.hpp"
#include <string>

class PMXLoader
{
public:
	// PMX‚ğ“Ç‚ñ‚Å ModelCpuData ‚É—¬‚µ‚Ş
	static bool Parse(const std::string& filepath, float scale, ModelCpuData& out);
};