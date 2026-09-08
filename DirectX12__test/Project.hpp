/*****************************************************************//**
 * \file   Project.hpp
 * \brief  プロジェクト(Assets ルート)の作成・オープン・カレント管理
 * 
 * 作成者 
 * 作成日 2026/9/8
 * 更新履歴 9.8 作成
 * *********************************************************************/
#pragma once

#include <string>
#include <vector>
#include <filesystem>

#define PROJECT Project::GetInstance()

class Project
{
public:
	static Project* GetInstance();

	/// @brief 新規プロジェクトを作成して開く
	/// @param parentDir 作成先の親フォルダ
	/// @param name プロジェクト名
	/// @param outError 失敗した場合の失敗理由
	/// @return 成功時 true, 失敗時 false
	bool Create(
		_In_ const std::filesystem::path& parentDir,
		_In_ const std::string& name,
		_Out_ std::string& outError
	);

	/// @brief 既存プロジェクトを開く(.dxproj か、それを含むフォルダ)
	/// @param path 開くプロジェクトのパス
	/// @param outError 失敗した場合の失敗理由
	/// @return 成功時 true, 失敗時 false
	bool Open(
		_In_ const std::filesystem::path& path,
		_Out_ std::string& outError
	);

	/// @brief 開いているかどうか
	/// @return 開けた場合 true, 開けなかった場合 false
	bool IsOpen() const noexcept { return !m_Root.empty(); }

	const std::filesystem::path& GetRoot() const noexcept { return m_Root; }
	const std::string& GetName() const noexcept { return m_Name; }
	const std::string& GetStartScene() const noexcept { return m_StartScene; }

	/// @brief プロジェクト相対パスを絶対パスに変換する ("Assets/x.png" → root/Assets/x.png)
	/// @param relative プロジェクト相対パス
	/// @return 絶対パス
	std::filesystem::path Resolve(const std::string& relative) const;
private:
	std::filesystem::path m_Root; // プロジェクトのルートフォルダ
	std::string m_Name; // プロジェクト名
	std::vector<std::string> m_Recents; // 最近開いたプロジェクトのパス
	std::string m_StartScene = "Assets/Scenes/SampleScene.json"; // 起動時に開くシーンのパス

	Project() = default;
	Project(const Project&) = delete;
	void operator=(const Project&) = delete;
};

