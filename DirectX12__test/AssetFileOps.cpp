/*! ************************************************************
 * \file   AssetFileOps.cpp
 * \brief  Assets フォルダに対するファイル操作(UI 非依存)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "AssetFileOps.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <Windows.h>
#include <shellapi.h>

#include "Logger.hpp"
#include "Project.hpp"
#include "json.hpp"
#include "AssetDatabase.hpp"

using json = nlohmann::json;

namespace fs = std::filesystem;

void AssetFileOps::CreateScriptFile(const std::string& dir, const std::string& name)
{
	fs::path hpp = fs::absolute(fs::path(dir)) / (name + ".hpp");
	fs::path cpp = fs::absolute(fs::path(dir)) / (name + ".cpp");
	if (fs::exists(hpp))
	{
		LOG->LogWarning("スクリプトファイルが既に存在します: " + hpp.string());
	}
	if(fs::exists(cpp))
	{
		LOG->LogWarning("スクリプトファイルが既に存在します: " + cpp.string());
	}

	// UTF-8 BOM を付ける。無いと MSVC がシステムのコードページ(932)として読み、
	// 日本語コメントの末尾が改行を飲み込んで次の行がコメント扱いになる
	static const char* kUtf8Bom = "\xEF\xBB\xBF";

	std::ofstream out(hpp, std::ios::binary);
	out << kUtf8Bom;
	out <<
		"#pragma once\n"
		"#include \"MonoBehavior.hpp\"\n"
		"#include \"Logger.hpp\"\n"
		"#include \"Components.hpp\"\n\n"
		"class " << name << " : public MonoBehavior\n"
		"{\n"
		"public:\n"
		"    void OnStart() override {}\n"
		"    void OnUpdate(float dt) override\n"
		"    {\n"
		"        // auto& tr = transform();\n"
		"        LOG->LogInfo(\"[" << name << "] Update\");\n"
		"    }\n"
		"};\n";

	std::ofstream outcpp(cpp, std::ios::binary);
	outcpp << kUtf8Bom;
	outcpp <<
		"#include \"" << name << ".hpp\"\n"
		"#include \"RegisterScript.hpp\"\n\n"
		"REGISTER_SCRIPT(" << name << ");\n";

	out.close();
	outcpp.close();

	// vcxproj への登録は不要。
	// ビルド直前に Project::RefreshScriptProjectSources が
	// Assets 配下を走査して一覧を作り直す
	LOG->LogInfo("スクリプト生成: " + hpp.string());
}

void AssetFileOps::OpenInEditor(const std::string& path)
{

	const fs::path file = fs::absolute(path);

	// VS から見えるように、開く前にファイル一覧を作り直しておく。
	// これをしないと未ビルドのプロジェクトでは vcxproj が空で、
	// 開いた .cpp が「その他のファイル」扱いになり IntelliSense が効かない
	{
		std::string err;
		if (!PROJECT->RefreshScriptProjectSources(err))
		{
			LOG->LogWarning(err);
		}
	}

	const fs::path sln = PROJECT->GetScriptSolutionPath();

	// プロジェクトの sln があればそれごと開く。
	// エンジンの sln とは別ファイルなので VS が別インスタンスで立ち上がる。
	// /edit は「起動中のVSで開く」動作なのでここでは使わない
	if (fs::exists(sln))
	{
		const std::wstring args =
			L"\"" + sln.wstring() + L"\" \"" + file.wstring() + L"\"";
		ShellExecuteW(nullptr, L"open", L"devenv.exe", args.c_str(),
			nullptr, SW_SHOWNORMAL);
		return;
	}

	// sln が無いときは従来どおりファイル単体で開く
	const std::wstring args = L"/edit \"" + file.wstring() + L"\"";
	ShellExecuteW(nullptr, L"open", L"devenv.exe", args.c_str(),
		nullptr, SW_SHOWNORMAL);
}

void AssetFileOps::CreateFolder(const std::string& dir)
{

	// "New Folder", "New Folder 1", ... と重複回避
	fs::path target = fs::path(dir) / "New Folder";
	int n = 1;
	while (fs::exists(target))
		target = fs::path(dir) / ("New Folder " + std::to_string(n++));

	std::error_code ec;
	fs::create_directory(target, ec);
	if (ec) LOG->LogWarning("フォルダ作成失敗: " + ec.message());
	else    LOG->LogInfo("フォルダ作成: " + target.string());
}

void AssetFileOps::ImportAssets(const std::string& destDir_, const std::vector<std::string>& sources)
{
	const fs::path destDir = destDir_;

	std::error_code ec;
	fs::create_directories(destDir, ec);

	int copied = 0;
	for (const auto& src : sources)
	{
		const fs::path from = src;
		if (!fs::exists(from, ec)) continue;

		// たプロジェクトの .meta を持ち込むとGUIDが衝突する
		if (from.extension() == ".meta") continue;

		// プロジェクト内のものを投げ込まれた場合は何もしない(自分自身への複製を防ぐ)
		const fs::path fromAbs = fs::weakly_canonical(from, ec);
		const fs::path destAbs = fs::weakly_canonical(destDir, ec);
		if (fromAbs == destAbs || fromAbs == destAbs / from.filename())
		{
			continue;
		}

		// 同名があれば "Foo 1" のように退避する(黙って上書きしない)
		fs::path to = destDir / from.filename();
		if (fs::exists(to, ec))
		{
			const std::string stem = from.stem().string();
			const std::string ext = from.extension().string();
			int n = 1;
			do
			{
				to = destDir / (stem + " " + std::to_string(n++) + ext);
			} while (fs::exists(to, ec));
		}

		ec.clear();
		if (fs::is_directory(from, ec))
		{
			fs::copy(from, to, fs::copy_options::recursive, ec);
		}
		else
		{
			fs::copy_file(from, to, ec);
		}

		if (ec)
		{
			LOG->LogWarning("取り込みに失敗: " + from.string() + " (" + ec.message() + ")");
			continue;
		}

		LOG->LogInfo("取り込み: " + to.string());
		ASSETDB->OnAssetAdded(to.generic_string());
		++copied;
	}

	if (copied > 0)
	{
		LOG->LogInfo("取り込み完了: " + std::to_string(copied) + " 件");
	}
}

void AssetFileOps::RevealInExplorer(const std::string& path)
{

	const fs::path target = fs::absolute(path);
	if (!fs::exists(target))
	{
		LOG->LogWarning("エクスプローラーで開けません: " + target.string());
		return;
	}

	if (fs::is_directory(target))
	{
		ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	}
	else
	{
		// ファイルは選択状態で開く
		const std::wstring args = L"/select,\"" + target.wstring() + L"\"";
		ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
	}
}

void AssetFileOps::DuplicateAsset(const std::string& path)
{

	const fs::path src = path;
	const fs::path dir = src.parent_path();
	const std::string stem = src.stem().string();
	const std::string ext = src.extension().string();

	// "Foo" → "Foo 1", "Foo 2", ... と重複回避
	fs::path dst;
	int n = 1;
	do
	{
		dst = dir / (stem + " " + std::to_string(n++) + ext);
	} while (fs::exists(dst));

	std::error_code ec;
	if (fs::is_directory(src))
	{
		fs::copy(src, dst, fs::copy_options::recursive, ec);
	}
	else
	{
		fs::copy_file(src, dst, ec);
	}

	if (ec) LOG->LogWarning("複製に失敗: " + ec.message());
	else
	{
		LOG->LogInfo("複製: " + dst.string());
		ASSETDB->OnAssetDuplicated(src.generic_string(),
			dst.generic_string());
	}
}

void AssetFileOps::RenameAsset(const std::string& path, const std::string& newName)
{

	const fs::path src = path;
	fs::path dst = src.parent_path() / newName;

	// 拡張子を省略された場合は元の拡張子を引き継ぐ
	if (!fs::is_directory(src) && dst.extension().empty())
	{
		dst += src.extension();
	}

	if (fs::exists(dst))
	{
		LOG->LogWarning("同名のファイルが既にあります: " + dst.string());
		return;
	}

	std::error_code ec;
	fs::rename(src, dst, ec);
	if (ec) LOG->LogWarning("リネームに失敗: " + ec.message());
	else
	{
		LOG->LogInfo("リネーム: " + src.string() + " -> " + dst.string());
		ASSETDB->OnAssetMoved(src.generic_string(),
			dst.generic_string());
	}
}

void AssetFileOps::DeleteAsset(const std::string& path)
{
	// 実態を消す前に DB から外す(.metaもここで消える)
	ASSETDB->OnAssetRemoved(path);

	std::error_code ec;
	const std::uintmax_t removed = fs::remove_all(path, ec);

	if (ec) LOG->LogWarning("削除に失敗: " + ec.message());
	else    LOG->LogInfo("削除: " + path + " (" + std::to_string(removed) + " 件)");
}

void AssetFileOps::CreateSceneFile(const std::string& dir)
{

	fs::path target = fs::path(dir) / "NewScene.json";
	int n = 1;
	while (fs::exists(target))
		target = fs::path(dir) / ("NewScene" + std::to_string(n++) + ".json");

	// Project::WriteEmptyScene と同じ最小構成
	// (SceneSerializer::LoadFromString が要求するのは entities だけ)
	json j;
	j["sceneName"] = target.stem().string();
	j["entities"] = json::array();

	std::ofstream ofs(target);
	if (!ofs)
	{
		LOG->LogWarning("シーン作成に失敗: " + target.string());
		return;
	}
	ofs << j.dump(4);
	ofs.close();
	ASSETDB->OnAssetAdded(target.generic_string());
	LOG->LogInfo("シーン作成: " + target.string());
}
