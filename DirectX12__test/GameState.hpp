/*****************************************************************//**
 * \file   GameState.hpp
 * \brief  シーンをまたいで残る値の置き場(セーブデータ兼用)
 *
 * 作成者 keep
 * 作成日 2026/9/10
 * 更新履歴 9/10 作成
 * *********************************************************************/
#pragma once

#include <string>
#include "EngineAPI.hpp"

#define GAMESTATE GameState::GetInstance()

/// @brief シーンを切り替えても消えない値のストア
/// @note LoadScene で World は作り直されるため、スコアやステージ番号は
///       ここへ置く。Save でファイルへ落とせばセーブデータになる。
///       exe に実体を1つだけ置き、スクリプト(DLL)からは
///       ENGINE_API 経由で同じものを触る(ヘッダ内 static にすると別実体になる)
class ENGINE_API GameState
{
public:
	static GameState* GetInstance();

	void SetInt(_In_ const std::string& key, _In_ int value);
	void SetFloat(_In_ const std::string& key, _In_ float value);
	void SetBool(_In_ const std::string& key, _In_ bool value);
	void SetString(_In_ const std::string& key, _In_ const std::string& value);

	int         GetInt(_In_ const std::string& key, _In_ int def = 0) const;
	float       GetFloat(_In_ const std::string& key, _In_ float def = 0.0f) const;
	bool        GetBool(_In_ const std::string& key, _In_ bool def = false) const;
	std::string GetString(_In_ const std::string& key, _In_ const std::string& def = "") const;

	bool Has(_In_ const std::string& key) const;
	void Erase(_In_ const std::string& key);

	/// @brief 全部消す
	/// @note タイトルへ戻るときなどに呼ぶ。セーブファイルは消えない
	void Clear();

	/// @brief プロジェクト直下の save/<name>.json へ書き出す
	/// @return 成功なら true
	bool Save(_In_ const std::string& name = "save") const;

	/// @brief 読み込む
	/// @return ファイルが無い場合も false(中身は変更しない)
	bool Load(_In_ const std::string& name = "save");

private:
	GameState() = default;
	GameState(const GameState&) = delete;
	void operator=(const GameState&) = delete;

	struct Impl;
	Impl* m_Impl = nullptr;

	Impl& Data() const;
};
