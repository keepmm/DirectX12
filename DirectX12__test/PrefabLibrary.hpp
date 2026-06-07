/*****************************************************************//**
 * \file   PrefabLibrary.hpp
 * \brief  Prefab管理
 * 
 * 作成者 keeeep
 * 作成日 2026/5/22
 * 更新履歴	5.22 作成
 *			5.23 プレハブをシーン共通にできるように追加
 *			5.25 GUIDからプレハブを生成できるように追加
 * *********************************************************************/
#pragma once

#include "World.hpp"
#include "Scene.hpp"

class PrefabLibrary
{
public:
	using PrefabBuilder = std::function<void(Scene&,World&, Entity)>;

	/// @brief プレハブのインスタンスを取得
	/// @return ポインタ
	static PrefabLibrary& Get();

	/// @brief プレハブの登録(guidとなまえ)
	/// @param name プレハブの名前
	/// @param guid プレハブのGUID
	/// @param builder ビルダー関数
	void RegisterPrefab(
		_In_ const std::string& name,
		_In_ const std::string& guid,
		_In_ PrefabBuilder builder);

	/// @brief プレハブの登録
	/// @param name 登録するプレハブの名前
	/// @param builder ビルダー関数
	void RegisterPrefab(
		_In_ const std::string& name,
		_In_ PrefabBuilder builder);

	/// @brief 既にプレハブが登録されているか(名前)
	/// @param name 検索するプレハブのなまえ　
	/// @return 存在する場合はtrue
	bool HasPrefab(_In_ const std::string& name) const;

	/// @brief 既にプレハブにGUIDが登録されているか
	/// @param guid 検索するguid
	/// @return 存在する場合はtrue
	bool HasPrefabGuid(_In_ const std::string& guid) const;

	/// @brief インスタンス生成
	/// @param name 生成するプレハブの名前
	/// @param world 生成する情報
	/// @return 生成されたエンティティ
	Entity Instantiate(
		_In_ const std::string& name, 
		_In_ Scene& scene,
		_In_ World& world) const;

	Entity InstantiateByGuid(
		_In_ const std::string& guid,
		_In_ Scene& scene,
		_In_ World& world) const;

	/// @brief 登録されているプレハブの名前の取得
	/// @return 文字列のベクター
	std::vector<std::string> GetPrefabNames() const;

	std::string GetPrefabGuid(const std::string& name) const;
	std::string GetPrefabNameByGuid(const std::string& guid) const;

private:
	std::string MakeGuidFromName(const std::string& name) const;

	std::unordered_map<std::string, PrefabBuilder> m_Prefabs;
	std::unordered_map<std::string, std::string> m_PrefabGuids;
	std::unordered_map<std::string, std::string> m_GuidtoPrefab;
};

