/*! ************************************************************
 * \file   EntityFactory.hpp
 * \brief  よく使う Entity の組み立て(UI 非依存)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 *
 * \note ImGui / EditorWindow に依存しない。World を触るだけなので
 *       エディタのメニュー以外(ランタイム・スクリプト)からも呼べる
 * *********************************************************************/
#pragma once

#include <string>

#include "World.hpp"
#include "Components.hpp"
#include "Defines.hpp"

class Scene;

namespace EntityFactory
{
	/// @brief Canvas の検索または作成
	/// @param world worldの参照
	/// @return 既に存在する場合はそのID、無ければ新規作成したID
	Entity EnsureCanvas(_In_ World& world);

	/// @brief UI Image の作成(Canvas が無ければ同時に作る)
	/// @return 作成したEntityのID
	Entity CreateImage(_In_ World& world);

	/// @brief UI Text の作成(Canvas が無ければ同時に作る)
	/// @return 作成したEntityのID
	Entity CreateText(_In_ World& world);

	/// @brief 組み込みプリミティブ(Cube / Sphere)の作成
	/// @param tag Util.hpp の kPrimitiveCube / kPrimitiveSphere
	Entity CreatePrimitive(_In_ World& world, _In_ const std::string& tag);

	/// @brief モデルファイルから Entity を作る
	/// @param pos 配置するワールド座標
	/// @return 作成したEntityのID(選択状態にするかは呼び出し側の判断)
	Entity SpawnModelFromFile(
		_In_ World& world,
		_In_ const std::string& modelPath,
		_In_ const float3& pos,
		_In_ Scene* scene);
}
