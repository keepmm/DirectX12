/*!*************************************************************
 * \file   AssetExt.hpp
 * \brief  エンジンが扱うアセットの拡張子
 *
 * 作成者 keeep
 * 作成日 2026/9/14
 * 更新履歴	9.14 作成
 * *********************************************************************/
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace AssetExt
{
	inline constexpr const char* Scene       = ".scene";
	inline constexpr const char* Material    = ".mat";
	inline constexpr const char* LegacyScene = ".json";	// 旧形式。読み込みだけ受ける

	/// @brief 拡張子が一致するか(大文字小文字を区別しない)
	inline bool Is(const std::string& path, const char* ext)
	{
		std::string e = std::filesystem::path(path).extension().string();
		std::transform(e.begin(), e.end(), e.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return e == ext;
	}
}
