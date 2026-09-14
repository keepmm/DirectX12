/*! ************************************************************
 * \file   AssetFileOps.hpp
 * \brief  Assets フォルダに対するファイル操作(UI 非依存)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 *
 * \note ImGui / EditorWindow に依存しない。std::filesystem と
 *       ShellExecute だけで完結するので、エディタ以外からも呼べる
 * *********************************************************************/
#pragma once

#include <string>
#include <vector>

namespace AssetFileOps
{
	/// @brief MonoBehavior 派生のスクリプト(.hpp/.cpp)を生成する
	/// @param dir  生成先ディレクトリ
	/// @param name クラス名 兼 ファイル名
	/// @note vcxproj への登録は不要。ビルド直前に
	///       Project::RefreshScriptProjectSources が Assets 配下を走査し直す
	void CreateScriptFile(const std::string& dir, const std::string& name);

	/// @brief Visual Studio でファイルを開く(プロジェクトの sln があればそれごと)
	void OpenInEditor(const std::string& path);

	/// @brief "New Folder", "New Folder 1", ... と重複を避けてフォルダを作る
	void CreateFolder(const std::string& dir);

	/// @brief 外部から来たファイル / フォルダを取り込む
	/// @param destDir 取り込み先(現在のアセットフォルダ)
	/// @param sources ドロップされた絶対パス
	void ImportAssets(const std::string& destDir, const std::vector<std::string>& sources);

	/// @brief エクスプローラーで開く(フォルダならその中、ファイルなら選択状態)
	void RevealInExplorer(const std::string& path);

	/// @brief 複製を作る("Foo.png" → "Foo 1.png")
	void DuplicateAsset(const std::string& path);

	/// @brief リネーム(失敗時はログのみ)
	void RenameAsset(const std::string& path, const std::string& newName);

	/// @brief 削除(フォルダは中身ごと)
	void DeleteAsset(const std::string& path);

	/// @brief 空のシーン json を作る
	void CreateSceneFile(const std::string& dir);
}
