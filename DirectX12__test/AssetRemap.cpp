/*!*************************************************************
 * \file   AssetRemap.cpp
 * \brief  World 内のアセット参照パスを一括で張り替える
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 作成
 * *********************************************************************/
#include "AssetRemap.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "Components.hpp"
#include "MonoBehavior.hpp"
#include "ComponentRegistry.hpp"
#include "Logger.hpp"
#include "Scene.hpp"
#include "ScriptField.hpp"

namespace
{
	/// @brief 比較用にパスを揃える(区切り / 大小文字)
	std::string Norm(const std::string& s)
	{
		std::string out = std::filesystem::path(s).lexically_normal().generic_string();
		if (out.rfind("./", 0) == 0) out.erase(0, 2);
		std::transform(out.begin(), out.end(), out.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	/// @brief 1 本のパス文字列を置換する。'|' 区切りの複数もそのまま扱える
	/// @return 置換したら true
	bool ReplaceOne(std::string& target, const std::string& oldNorm, const std::string& newPath)
	{
		if (target.empty()) return false;

		if (target.find('|') == std::string::npos)
		{
			if (Norm(target) != oldNorm) return false;
			target = newPath;
			return true;
		}

		// '|' 区切り(AnimatorComponent::ClipPaths)
		std::string joined;
		bool hit = false;
		std::stringstream ss(target);
		std::string token;
		while (std::getline(ss, token, '|'))
		{
			if (!token.empty() && Norm(token) == oldNorm) { token = newPath; hit = true; }
			if (!joined.empty()) joined += '|';
			joined += token;
		}
		if (hit) target = joined;
		return hit;
	}
}

int AssetRemap::Remap(World& world, Scene& scene,
	const std::string& oldPath, const std::string& newPath)
{
	if (oldPath.empty() || newPath.empty() || oldPath == newPath) return 0;

	const std::string oldNorm = Norm(oldPath);
	int count = 0;

	for (Entity e : world.GetEntities())
	{
		// ---- ComponentRegistry に載っているもの(Reflect 経由) ---- //
		for (const auto& meta : ComponentRegistry::All())
		{
			if (!meta.reflect || !meta.has(world, e)) continue;

			FieldList fl;
			meta.reflect(world, e, fl);
			for (const auto& f : fl.fields)
			{
				if (!IsAssetField(f.type)) continue;
				if (ReplaceOne(*(std::string*)f.ptr, oldNorm, newPath)) ++count;
			}
		}

		// ---- Registry に載っていない手書きのもの ---- //
		if (world.HasComponent<MeshComponent>(e))
		{
			auto& m = world.GetComponent<MeshComponent>(e);
			if (ReplaceOne(m.FilePath, oldNorm, newPath)) ++count;
		}

		if (world.HasComponent<MaterialComponent>(e))
		{
			auto& m = world.GetComponent<MaterialComponent>(e);
			if (ReplaceOne(m.FilePath, oldNorm, newPath)) ++count;
			if (ReplaceOne(m.RampFilePath, oldNorm, newPath)) ++count;
			for (auto& asset : m.materialAssets)
			{
				if (ReplaceOne(asset, oldNorm, newPath)) ++count;
			}
		}

		// ---- スクリプトのフィールド値 ---- //
		if (world.HasComponent<ScriptComponent>(e))
		{
			auto& sc = world.GetComponent<ScriptComponent>(e);
			for (auto& [scriptName, fields] : sc.values)
			{
				for (auto& [fieldName, v] : fields)
				{
					if (!IsAssetField(v.type)) continue;
					if (ReplaceOne(v.s, oldNorm, newPath)) ++count;
				}
			}
		}
	}

	// ---- シーンのスカイボックス ---- //
	{
		std::string sky = scene.GetSkyboxPath();
		if (ReplaceOne(sky, oldNorm, newPath))
		{
			scene.SetSkyboxPath(sky);
			++count;
		}
	}

	if (count > 0)
	{
		LOG->LogInfo("参照を張り替えました: " + oldPath + " -> " + newPath
			+ " (" + std::to_string(count) + " 件)");
	}
	return count;
}