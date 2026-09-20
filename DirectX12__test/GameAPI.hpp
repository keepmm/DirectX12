#pragma once
#include "Defines.hpp"
#include "ScriptContext.hpp"

#include <vector>

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

	// ------------------------------------------------------------------ //
	//                             物理                                    //
	// ------------------------------------------------------------------ //

	struct RaycastHit
	{
		std::uint32_t entity = 0;
		float3 point{};
		float3 normal{};
		float distance = 0.0f;
	};

	inline bool (*g_Raycast)(const float*, const float*, float, std::uint32_t, RaycastHitRaw*) = nullptr;
	inline int  (*g_OverlapSphere)(const float*, float, std::uint32_t, std::uint32_t*, int) = nullptr;
	inline bool (*g_AddForce)(std::uint32_t, float, float, float, bool) = nullptr;
	inline bool (*g_SetVelocity)(std::uint32_t, float, float, float) = nullptr;
	inline bool (*g_GetVelocity)(std::uint32_t, float*) = nullptr;

	/// @brief 光線を飛ばして最初に当たったものを調べる
	/// @param layerMask 調べたいレイヤーのビットマスク(1 << レイヤー番号 を足し合わせる)
	/// @note トリガーには当たらない。自分自身にも当たるので、始点は体の外に出すか結果を見て弾く
	inline bool Raycast(const float3& origin, const float3& direction,
		float maxDistance, RaycastHit& outHit, std::uint32_t layerMask = 0xFFFFFFFFu)
	{
		if (!g_Raycast) return false;

		const float o[3] = { origin.x, origin.y, origin.z };
		const float d[3] = { direction.x, direction.y, direction.z };
		RaycastHitRaw raw{};
		if (!g_Raycast(o, d, maxDistance, layerMask, &raw)) return false;

		outHit.entity = raw.entity;
		outHit.point = { raw.point[0], raw.point[1], raw.point[2] };
		outHit.normal = { raw.normal[0], raw.normal[1], raw.normal[2] };
		outHit.distance = raw.distance;
		return true;
	}

	/// @brief 球の中に重なっているものを集める(攻撃範囲の判定など)
	/// @param maxCount 受け取る上限
	inline std::vector<std::uint32_t> OverlapSphere(const float3& center, float radius,
		std::uint32_t layerMask = 0xFFFFFFFFu, int maxCount = 32)
	{
		std::vector<std::uint32_t> out;
		if (!g_OverlapSphere || maxCount <= 0) return out;

		out.resize(maxCount);
		const float c[3] = { center.x, center.y, center.z };
		const int n = g_OverlapSphere(c, radius, layerMask, out.data(), maxCount);
		out.resize(n < 0 ? 0 : n);
		return out;
	}

	/// @brief 力を加える(Rigid Body が Dynamic のときだけ効く)
	/// @param impulse true なら瞬間的な衝撃(ノックバック)、false なら押し続ける力
	inline bool AddForce(std::uint32_t entity, const float3& force, bool impulse = true)
	{
		return g_AddForce ? g_AddForce(entity, force.x, force.y, force.z, impulse) : false;
	}

	inline bool SetVelocity(std::uint32_t entity, const float3& velocity)
	{
		return g_SetVelocity ? g_SetVelocity(entity, velocity.x, velocity.y, velocity.z) : false;
	}

	inline bool GetVelocity(std::uint32_t entity, float3& outVelocity)
	{
		if (!g_GetVelocity) return false;
		float v[3]{};
		if (!g_GetVelocity(entity, v)) return false;
		outVelocity = { v[0], v[1], v[2] };
		return true;
	}
}