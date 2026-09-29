/*!*************************************************************
 * ile   EditorContext.hpp
 * rief  エディタのパネル間で共有する状態
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow の分割にあわせて作成
 *
 * 
ote 「どのエンティティを選んでいるか」のようにパネルをまたぐ値だけを置く。
 *       1 つのパネルでしか使わない値は、そのパネルのメンバにする
 * *********************************************************************/
#pragma once

#include <string>

#include "imguiinit.hpp"
#include "World.hpp"
#include "Scene.hpp"

class DirectXApp;
class SceneManager;
class UndoHistory;

struct EditorContext
{
	EditorContext(DirectXApp& a, SceneManager& sm) : app(a), sceneManager(sm) {}

	DirectXApp&   app;
	SceneManager& sceneManager;

	/// @brief このフレームのアクティブシーン(切り替え中は nullptr になりうる)
	Scene* activeScene = nullptr;

	Entity selectedEntity = INVALID_ENTITY;

	UndoHistory* history = nullptr;
	std::string selectedAsset;
	std::string currentAssetDir = "Assets";

	// ビューポート情報(ViewportPanel が毎フレーム更新する)
	ImVec2 viewportPos{ 0.0f, 0.0f };
	ImVec2 viewportSize{ 0.0f, 0.0f };

	/// @brief アクティブシーンの World(無ければ nullptr)
	World* world() const
	{
		return activeScene ? &activeScene->GetWorld() : nullptr;
	}
};
