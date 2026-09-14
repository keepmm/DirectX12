/*!*************************************************************
 * \file   AssetDatabase.cpp
 * \brief  Assets 配下のアセットに GUID を振り、.meta で追従させる
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 作成
 * *********************************************************************/
#include "AssetDatabase.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

#include "Logger.hpp"
#include "Project.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
	constexpr int kMetaVersion = 1;
	constexpr const char* kMetaExt = ".meta";

	/// @brief ランダム 128bit の GUID を 32 桁小文字 hex で作る
	/// @note PrefabLibrary::MakeGuidFromName は名前から決定的に作るので流用しない。
	///       リネームで GUID が変わっては .meta を置く意味 がなくなる
	AssetGuid MakeRandomGuid()
	{
		static std::mt19937_64 engine(std::random_device{}());
		std::uniform_int_distribution<std::uint64_t> dist;

		char buf[33] = {};
		std::snprintf(buf, sizeof(buf), "%016llx%016llx",
			static_cast<unsigned long long>(dist(engine)),
			static_cast<unsigned long long>(dist(engine)));
		return AssetGuid(buf);
	}

	fs::path MetaPathOf(const fs::path& assetPath)
	{
		return fs::path(assetPath.native() + std::filesystem::path(kMetaExt).native());
	}

	/// @brief .meta から guid を読む。壊れていれば空
	AssetGuid ReadMetaGuid(const fs::path& metaPath)
	{
		std::ifstream in(metaPath);
		if (!in) return {};

		try
		{
			json j; in >> j;
			return j.value("guid", std::string{});
		}
		catch (const std::exception&)
		{
			LOG->LogWarning(".meta の読み込みに失敗: " + metaPath.string());
			return {};
		}
	}

	bool WriteMeta(const fs::path& metaPath, const AssetGuid& guid)
	{
		json j;
		j["metaVersion"] = kMetaVersion;
		j["guid"] = guid;
		j["importer"] = json::object();   // 将来の sRGB / mip / スケール等の置き場

		std::ofstream out(metaPath);
		if (!out)
		{
			LOG->LogWarning(".meta の書き出しに失敗: " + metaPath.string());
			return false;
		}
		out << j.dump(2);
		return true;
	}
}

AssetDatabase& AssetDatabase::Get()
{
	static AssetDatabase instance;
	return instance;
}

// ------------------------------------------------------------------ //
//                            キーの正規化                             //
// ------------------------------------------------------------------ //

std::string AssetDatabase::NormalizeKey(const std::string& path)
{
	if (path.empty()) return {};

	std::error_code ec;
	fs::path p(path);

	// 絶対ならプロジェクトルート相対へ畳む
	if (p.is_absolute() && PROJECT->IsOpen())
	{
		const fs::path rel = fs::relative(p, PROJECT->GetRoot(), ec);
		if (!ec && !rel.empty() && rel.native().rfind(L"..", 0) != 0)
		{
			p = rel;
		}
	}

	std::string s = p.lexically_normal().generic_string();

	// "./Assets/..." の先頭を落とす
	if (s.rfind("./", 0) == 0) s.erase(0, 2);

	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool AssetDatabase::IsIgnored(const fs::path& absPath)
{
	const std::string ext = absPath.extension().string();
	if (ext == kMetaExt) return true;

	// スクリプトのソースは Project::RefreshScriptProjectSources の領域。
	// .meta を撒いても使い道がないので触らない
	const std::string generic = absPath.generic_string();
	if (generic.find("/Assets/Scripts/") != std::string::npos)
	{
		if (ext == ".cpp" || ext == ".hpp" || ext == ".h") return true;
	}

	// 隠しファイル
	const std::string name = absPath.filename().string();
	if (!name.empty() && name[0] == '.') return true;

	return false;
}

// ------------------------------------------------------------------ //
//                                走査                                 //
// ------------------------------------------------------------------ //

void AssetDatabase::Refresh()
{
	m_GuidToPath.clear();
	m_PathToGuid.clear();
	m_Ready = false;

	if (!PROJECT->IsOpen()) return;

	const fs::path assetsRoot = PROJECT->GetRoot() / "Assets";
	std::error_code ec;
	if (!fs::exists(assetsRoot, ec))
	{
		LOG->LogWarning("AssetDatabase: Assets フォルダがありません");
		return;
	}

	std::vector<fs::path> metaFiles;

	for (auto it = fs::recursive_directory_iterator(assetsRoot, ec);
		it != fs::recursive_directory_iterator(); it.increment(ec))
	{
		if (ec) { ec.clear(); continue; }
		if (!it->is_regular_file(ec)) continue;

		const fs::path& p = it->path();
		if (p.extension() == kMetaExt) { metaFiles.push_back(p); continue; }
		if (IsIgnored(p)) continue;

		RegisterFile(p);
	}

	// 実体の無い .meta は消さずに警告だけ。
	// エディタ外で移動されたものを人が戻す余地を残す
	for (const auto& meta : metaFiles)
	{
		fs::path asset = meta;
		asset.replace_extension();   // "Foo.png.meta" → "Foo.png"
		if (!fs::exists(asset, ec))
		{
			LOG->LogWarning("孤立した .meta: " + meta.string());
		}
	}

	m_Ready = true;
	LOG->LogInfo("AssetDatabase: " + std::to_string(m_GuidToPath.size()) + " 件のアセットを登録");
}

void AssetDatabase::RegisterFile(const fs::path& absPath)
{
	std::error_code ec;
	const fs::path rel = fs::relative(absPath, PROJECT->GetRoot(), ec);
	if (ec) return;

	// コンポーネントの FilePath と同じ表現(generic_string)で持つ。
	// ここだけ UTF-8 にすると、ロード側の path(std::string) と食い違う
	const std::string relStr = rel.generic_string();
	const std::string key = NormalizeKey(relStr);

	const fs::path metaPath = MetaPathOf(absPath);
	AssetGuid guid = fs::exists(metaPath, ec) ? ReadMetaGuid(metaPath) : AssetGuid{};

	// 既に同じ GUID が登録されている場合、
	//  - 登録先と同じパス          → 登録し直しただけ。そのまま使う
	//  - 登録先のファイルがもう無い → 移動(リネーム)なので GUID を引き継ぐ
	//  - 登録先のファイルがまだある → 実体ごとコピーされた重複なので振り直す
	// 以前は「登録済みなら振り直す」だけだったので、フォルダをリネームすると
	// 中のアセットの GUID が全部変わり、シーンの参照が切れていた
	bool needNewGuid = guid.empty();
	if (!needNewGuid)
	{
		auto it = m_GuidToPath.find(guid);
		if (it != m_GuidToPath.end() && NormalizeKey(it->second) != key)
		{
			if (fs::exists(PROJECT->GetRoot() / fs::path(it->second), ec))
			{
				LOG->LogWarning("GUID が重複したので振り直します: " + relStr);
				needNewGuid = true;
			}
			else
			{
				// 移動前のキーを外す(新しいパスで下で登録し直す)
				m_PathToGuid.erase(NormalizeKey(it->second));
			}
		}
	}

	if (needNewGuid)
	{
		guid = MakeRandomGuid();
		WriteMeta(metaPath, guid);
	}

	m_GuidToPath[guid] = relStr;
	m_PathToGuid[key] = guid;
}

// ------------------------------------------------------------------ //
//                                参照                                 //
// ------------------------------------------------------------------ //

std::string AssetDatabase::GuidToPath(const AssetGuid& guid) const
{
	if (guid.empty()) return {};
	auto it = m_GuidToPath.find(guid);
	return (it != m_GuidToPath.end()) ? it->second : std::string{};
}

AssetGuid AssetDatabase::PathToGuid(const std::string& path) const
{
	if (path.empty()) return {};
	auto it = m_PathToGuid.find(NormalizeKey(path));
	return (it != m_PathToGuid.end()) ? it->second : AssetGuid{};
}

AssetGuid AssetDatabase::EnsureGuid(const std::string& path)
{
	if (path.empty() || !m_Ready) return {};

	if (AssetGuid guid = PathToGuid(path); !guid.empty()) return guid;

	// Refresh で拾えていないもの(Assets 外 / 存在しない / プリミティブの "@Cube")は
	// GUID を振らない。呼び出し側は path だけ書く
	std::error_code ec;
	const fs::path abs = fs::absolute(path, ec);
	if (ec || !fs::exists(abs, ec) || !fs::is_regular_file(abs, ec)) return {};
	if (NormalizeKey(path).rfind("assets/", 0) != 0) return {};

	RegisterFile(abs);
	return PathToGuid(path);
}

// ------------------------------------------------------------------ //
//                          ファイル操作の追従                         //
// ------------------------------------------------------------------ //

void AssetDatabase::OnAssetAdded(const std::string& path)
{
	if (!m_Ready) return;

	std::error_code ec;
	const fs::path abs = fs::absolute(path, ec);
	if (ec || !fs::exists(abs, ec)) return;

	if (fs::is_directory(abs, ec))
	{
		// フォルダごと取り込まれた場合は中身を登録する
		for (auto it = fs::recursive_directory_iterator(abs, ec);
			it != fs::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec) { ec.clear(); continue; }
			if (it->is_regular_file(ec) && !IsIgnored(it->path()))
				RegisterFile(it->path());
		}
		return;
	}

	if (!IsIgnored(abs)) RegisterFile(abs);
}

void AssetDatabase::OnAssetMoved(const std::string& from, const std::string& to)
{
	if (!m_Ready) return;

	std::error_code ec;
	const fs::path absFrom = fs::absolute(from, ec);
	const fs::path absTo = fs::absolute(to, ec);

	// フォルダなら配下を登録し直す。
	// 中の .meta はフォルダごと移動済みなので、RegisterFile が GUID を引き継ぐ
	if (fs::is_directory(absTo, ec))
	{
		OnAssetAdded(to);
		return;
	}

	// .meta を一緒に動かす。これで GUID が保たれる
	const fs::path metaFrom = MetaPathOf(absFrom);
	const fs::path metaTo = MetaPathOf(absTo);
	if (fs::exists(metaFrom, ec))
	{
		fs::rename(metaFrom, metaTo, ec);
		if (ec) LOG->LogWarning(".meta の移動に失敗: " + metaFrom.string());
	}

	const AssetGuid guid = PathToGuid(from);
	m_PathToGuid.erase(NormalizeKey(from));

	if (!guid.empty())
	{
		const fs::path rel = fs::relative(absTo, PROJECT->GetRoot(), ec);
		if (!ec)
		{
			m_GuidToPath[guid] = rel.generic_string();
			m_PathToGuid[NormalizeKey(rel.generic_string())] = guid;
			return;
		}
	}
	OnAssetAdded(to);
}

void AssetDatabase::OnAssetRemoved(const std::string& path)
{
	if (!m_Ready) return;

	std::error_code ec;
	const fs::path abs = fs::absolute(path, ec);

	// フォルダなら配下のキーを前方一致で外す
	const std::string key = NormalizeKey(path);
	const std::string prefix = key + "/";

	for (auto it = m_PathToGuid.begin(); it != m_PathToGuid.end(); )
	{
		if (it->first == key || it->first.rfind(prefix, 0) == 0)
		{
			m_GuidToPath.erase(it->second);
			it = m_PathToGuid.erase(it);
		}
		else ++it;
	}

	// .meta も消す(実体はもう消えている / これから消える)
	const fs::path meta = MetaPathOf(abs);
	if (fs::exists(meta, ec)) fs::remove(meta, ec);
}

void AssetDatabase::OnAssetDuplicated(const std::string& src, const std::string& dst)
{
	if (!m_Ready) return;

	// 複製元の .meta が一緒にコピーされていたら消す。
	// 同じ GUID が 2 つのファイルを指すのを防ぐ
	std::error_code ec;
	const fs::path metaDst = MetaPathOf(fs::absolute(dst, ec));
	if (fs::exists(metaDst, ec)) fs::remove(metaDst, ec);

	OnAssetAdded(dst);   // 新しい GUID が振られる
	(void)src;
}

// ------------------------------------------------------------------ //
//                        シリアライズ用ヘルパ                         //
// ------------------------------------------------------------------ //

namespace
{
	json OneRefToJson(const std::string& path)
	{
		json j;
		j["path"] = path;

		const AssetGuid guid = ASSETDB->EnsureGuid(path);
		if (!guid.empty()) j["guid"] = guid;
		return j;
	}

	std::string OneRefFromJson(const json& j)
	{
		// 旧形式: 生の文字列
		if (j.is_string()) return j.get<std::string>();
		if (!j.is_object()) return {};

		const std::string path = j.value("path", std::string{});
		const AssetGuid guid = j.value("guid", std::string{});

		if (!guid.empty() && ASSETDB->IsReady())
		{
			const std::string resolved = ASSETDB->GuidToPath(guid);
			if (!resolved.empty()) return resolved;

			LOG->LogWarning("参照が見つかりません(Missing): " + path + " [" + guid + "]");
		}
		// ゲーム版(未 Refresh)と GUID 未解決はパスへフォールバック
		return path;
	}
}

nlohmann::json AssetRefToJson(const std::string& path)
{
	if (path.empty()) return json(std::string{});

	// AnimatorComponent::ClipPaths のように '|' で複数入っているもの
	if (path.find('|') != std::string::npos)
	{
		json arr = json::array();
		std::stringstream ss(path);
		std::string token;
		while (std::getline(ss, token, '|'))
		{
			if (!token.empty()) arr.push_back(OneRefToJson(token));
		}
		return arr;
	}
	return OneRefToJson(path);
}

std::string AssetRefFromJson(const nlohmann::json& j)
{
	if (j.is_null()) return {};

	if (j.is_array())
	{
		std::string joined;
		for (const auto& e : j)
		{
			const std::string one = OneRefFromJson(e);
			if (one.empty()) continue;
			if (!joined.empty()) joined += '|';
			joined += one;
		}
		return joined;
	}
	return OneRefFromJson(j);
}

void WriteAssetRef(nlohmann::json& parent, const char* key, const std::string& path)
{
	parent[key] = AssetRefToJson(path);
}

std::string ReadAssetRef(const nlohmann::json& parent, const char* key)
{
	if (!parent.contains(key)) return {};
	return AssetRefFromJson(parent[key]);
}