/*****************************************************************//**
 * \file   AssetDatabase.hpp
 * \brief  Assets配下のアセットにGUIDを振り .metaで追従させる
 * 
 * 作成者 
 * 作成日 2026/9/12
 * 更新履歴
 * *********************************************************************/
#pragma once

#include <string>
#include <unordered_map>

#include "json.hpp"

using AssetGuid = std::string;

class AssetDatabase
{
public:
	static AssetDatabase& Get();

	/// @brief Assets配下を走査して .metaを作る / 読む
	void Refresh();

	bool IsReady() const noexcept { return m_Ready; }

	/// @brief GUID -> 現在のプロジェクト相対パス
	/// @param guid
	/// @return 現在のプロジェクト相対パス
	std::string GuidToPath(_In_ const AssetGuid& guid) const;

	/// @brief パス -> GUID
	AssetGuid PathToGuid(_In_ const std::string& path) const;

	AssetGuid EnsureGuid(_In_ const std::string& path);


	void OnAssetAdded(_In_ const std::string& path);

	void OnAssetMoved(_In_ const std::string& from, _In_ const std::string& to);

	void OnAssetRemoved(_In_ const std::string& path);

	void OnAssetDuplicated(_In_ const std::string& src, _In_ const std::string& dst);
private:
	AssetDatabase() = default;

	void RegisterFile(_In_ const std::filesystem::path& absPath);

	static std::string NormalizeKey(_In_ const std::string& path);

	static bool IsIgnored(_In_ const std::filesystem::path& absPath);

	std::unordered_map<AssetGuid,std::string> m_GuidToPath;	// guid -> "Assets/,,"
	std::unordered_map<std::string,AssetGuid> m_PathToGuid;	// 正規化キー -> guid
	bool m_Ready = false;
};

#define ASSETDB (&AssetDatabase::Get())

nlohmann::json AssetRefToJson(_In_ const std::string& path);

std::string AssetRefFromJson(_In_ const nlohmann::json& j);

void WriteAssetRef(
	_Inout_ nlohmann::json& parent,
	_In_ const char* key,
	_In_ const std::string& path
);

std::string ReadAssetRef(
	_In_ const nlohmann::json& parent,
	_In_ const char* key
);
