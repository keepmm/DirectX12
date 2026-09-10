#pragma once
#include "Defines.hpp"

namespace GameAPI
{
	inline void (*g_LaunchFirework)(float, float, float, int,
		float, float, float, const char*) = nullptr;

	// FireworkSystem::Shape と同じ並び
	enum FireworkShape { Peony = 0, Willow, Ring, Heart, Senrin, Text };

	inline void LaunchFirework(const float3& pos, FireworkShape shape,
		const float3& color, const char* text = "")
	{
		if (g_LaunchFirework)
			g_LaunchFirework(pos.x, pos.y, pos.z, (int)shape,
				color.x, color.y, color.z, text);
	}

	inline std::uint32_t(*g_Instantiate)(const char*) = nullptr;
	inline void         (*g_Destroy)(std::uint32_t) = nullptr;

	/// @brief プレハブから生成する
	/// @param prefabName PrefabLibrary に登録されている名前
	/// @return 生成した Entity。失敗時は 0(INVALID_ENTITY)
	inline std::uint32_t Instantiate(const char* prefabName)
	{
		return g_Instantiate ? g_Instantiate(prefabName) : 0u;
	}

	/// @brief Entity を破棄する
	/// @note 即時ではなくフレーム末にまとめて消える。
	///       更新中のコンテナを壊さないため
	inline void Destroy(std::uint32_t entity)
	{
		if (g_Destroy) g_Destroy(entity);
	}

	inline void (*g_LoadScene)(const char*, bool) = nullptr;

	/// @brief シーンを切り替える
	/// @param sceneName 拡張子もフォルダも付けない名前("MMDSample" など)
	/// @param withFade 暗転を挟むか
	/// @note 実際の切り替えはフレーム末(暗転の底)で起きる。呼んだ直後ではない。
	///       切り替えで World ごと作り直されるので、呼び出し元のEntityも消える
	inline void LoadScene(const char* sceneName, bool withFade = true)
	{
		if (g_LoadScene) g_LoadScene(sceneName, withFade);
	}
}