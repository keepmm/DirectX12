/*****************************************************************//**
 * \file   AssetRemap.hpp
 * \brief  World内のアセット参照パスを一括で張り替える
 * 
 * 作成者 
 * 作成日 2026/9/14
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <string>

#include "World.hpp"

class Scene;

namespace AssetRemap
{
	/// @brief oldPath を指している参照を newPath へ張り替える
	/// @param scene skybox の張り替えに使う
	/// @return 書き換えた件数
	int Remap(
		_In_ World& world,
		_In_ Scene& scene,
		_In_ const std::string& oldPath,
		_In_ const std::string& newPath);
}