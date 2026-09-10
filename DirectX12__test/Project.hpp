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

 /// @brief フォルダ選択ダイアログを出す
 /// @return 選択されたパス。キャンセル時は空文字
std::string PickProjectFolder();

/// @brief ランチャー(Launcher.exe)を起動する
/// @note exe 横にある想定。プロジェクトを選び直すときに使う
/// @return 起動できたら true
bool LaunchLauncher();

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

	/// @brief 生成される Scripts.vcxproj のパス
	std::filesystem::path GetScriptProjectPath() const { return m_Root / "Scripts.vcxproj"; }

	/// @brief 生成される Scripts.sln のパス
/// @note エンジンの sln とは別物にすることで VS が別インスタンスで開く
	std::filesystem::path GetScriptSolutionPath() const { return m_Root / "Scripts.sln"; }

	/// @brief Assets 配下の .cpp/.hpp を走査して Scripts.vcxproj の一覧を更新する
/// @note ビルド直前に呼ぶ。VS がワイルドカードを展開してしまうため一覧は実体で持つ
/// @return 更新した(または変更なしで正常)なら true
	bool RefreshScriptProjectSources(_Out_ std::string& outError);

	/// @brief Scripts.dll と cr の世代コピーの置き場
	std::filesystem::path GetLibraryDir() const { return m_Root / "Library"; }

	/// @brief Scripts.vcxproj が無ければ生成する
	/// @note 既存プロジェクトを開いたときにも呼ぶ。既にあれば何もしない
	/// @return 生成した or 既にある なら true
	bool EnsureScriptProject(_Out_ std::string& outError);

	/// @brief .dxproj ファイルを保存する
	/// @param outError 失敗した場合の失敗理由
	/// @return 成功時 true, 失敗時 false
	bool Save(_Out_ std::string& outError) const;

	/// @brief 最近開いたプロジェクト(新しい順)
	/// @return 最近開いたプロジェクトのパスのリスト
	const std::vector<std::string>& GetRecents() const noexcept { return m_Recents; }

	/// @brief 最近開いたプロジェクトを保存する
	void LoadRecents();
private:
	/// @brief Assets フォルダなどのプロジェクトの骨格を作成する
	/// @param root プロジェクトのルートフォルダ
	/// @param outError 失敗した場合の失敗理由
	/// @return 成功時 true, 失敗時 false
	bool CreateSkeleton(
		_In_ const std::filesystem::path& root,
		_Out_ std::string& outError
	);
	/// @brief 空のシーン (json) を書きだす
	/// @param scenePath シーンのパス
	/// @return 成功時 true, 失敗時 false
	bool WriteEmptyScene(_In_ const std::filesystem::path& scenePath) const;

	/// @brief カレントディレクトリを root に切り替える
	/// @param outError 失敗した場合の失敗理由
	/// @return 成功時 true, 失敗時 false
	bool Activate(_Out_ std::string& outError);

	/// @brief 最近開いたプロジェクトを追加する
	/// @param root プロジェクトのルートフォルダ
	void PushRecents(const std::filesystem::path& root);
	/// @brief 最近開いたプロジェクトを保存する
	void SaveRecents()const;

	std::filesystem::path m_Root; // プロジェクトのルートフォルダ
	std::string m_Name; // プロジェクト名
	std::vector<std::string> m_Recents; // 最近開いたプロジェクトのパス
	std::string m_StartScene = "Assets/Scenes/SampleScene.json"; // 起動時に開くシーンのパス

	Project() = default;
	Project(const Project&) = delete;
	void operator=(const Project&) = delete;
};

