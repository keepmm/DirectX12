/*****************************************************************//**
 * \file   GameState.cpp
 * \brief  シーンをまたいで残る値の置き場(セーブデータ兼用)
 *
 * 作成者 keep
 * 作成日 2026/9/10
 * 更新履歴 9/10 作成
 * *********************************************************************/
#include "GameState.hpp"
#include "json.hpp"

#include <unordered_map>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

struct GameState::Impl
{
	std::unordered_map<std::string, int>         ints;
	std::unordered_map<std::string, float>       floats;
	std::unordered_map<std::string, std::string> strings;
};

GameState* GameState::GetInstance()
{
	static GameState instance;
	return &instance;
}

GameState::Impl& GameState::Data() const
{
	// 遅延生成。const から呼べるように mutable 相当の扱いをする
	auto* self = const_cast<GameState*>(this);
	if (self->m_Impl == nullptr)
	{
		self->m_Impl = new Impl();
	}
	return *self->m_Impl;
}

void GameState::SetInt(const std::string& key, int value) { Data().ints[key] = value; }
void GameState::SetFloat(const std::string& key, float value) { Data().floats[key] = value; }
void GameState::SetBool(const std::string& key, bool value) { Data().ints[key] = value ? 1 : 0; }
void GameState::SetString(const std::string& key, const std::string& value) { Data().strings[key] = value; }

int GameState::GetInt(const std::string& key, int def) const
{
	const auto& m = Data().ints;
	auto it = m.find(key);
	return it != m.end() ? it->second : def;
}

float GameState::GetFloat(const std::string& key, float def) const
{
	const auto& m = Data().floats;
	auto it = m.find(key);
	return it != m.end() ? it->second : def;
}

bool GameState::GetBool(const std::string& key, bool def) const
{
	const auto& m = Data().ints;
	auto it = m.find(key);
	return it != m.end() ? it->second != 0 : def;
}

std::string GameState::GetString(const std::string& key, const std::string& def) const
{
	const auto& m = Data().strings;
	auto it = m.find(key);
	return it != m.end() ? it->second : def;
}

bool GameState::Has(const std::string& key) const
{
	const auto& d = Data();
	return d.ints.count(key) != 0 || d.floats.count(key) != 0 || d.strings.count(key) != 0;
}

void GameState::Erase(const std::string& key)
{
	auto& d = Data();
	d.ints.erase(key);
	d.floats.erase(key);
	d.strings.erase(key);
}

void GameState::Clear()
{
	auto& d = Data();
	d.ints.clear();
	d.floats.clear();
	d.strings.clear();
}

namespace
{
	// カレントはプロジェクトルートなので、そこの save/ に置く
	std::filesystem::path SavePath(const std::string& name)
	{
		return std::filesystem::path("save") / (name + ".json");
	}
}

bool GameState::Save(const std::string& name) const
{
	const auto& d = Data();

	json root;
	root["ints"] = d.ints;
	root["floats"] = d.floats;
	root["strings"] = d.strings;

	const auto path = SavePath(name);

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	std::ofstream ofs(path);
	if (!ofs) return false;

	ofs << root.dump(2);
	return true;
}

bool GameState::Load(const std::string& name)
{
	std::ifstream ifs(SavePath(name));
	if (!ifs) return false;

	json root = json::parse(ifs, nullptr, false);
	if (root.is_discarded()) return false;

	auto& d = Data();

	// 壊れたファイルで既存の値を消さないよう、読めた種類だけ差し替える
	if (root.contains("ints") && root["ints"].is_object())
		d.ints = root["ints"].get<std::unordered_map<std::string, int>>();
	if (root.contains("floats") && root["floats"].is_object())
		d.floats = root["floats"].get<std::unordered_map<std::string, float>>();
	if (root.contains("strings") && root["strings"].is_object())
		d.strings = root["strings"].get<std::unordered_map<std::string, std::string>>();

	return true;
}
