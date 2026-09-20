/*!*************************************************************
 * \file   HierarchyPanel.hpp
 * \brief  アウトライナー(シーン情報 + エンティティ一覧)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#pragma once

#include "EditorPanel.hpp"
#include "EditorContext.hpp"
#include <array>

class HierarchyPanel : public EditorPanel
{
public:
	const char* Title() const override
	{
		// u8() は一時 std::string の c_str() なので、返すと寿命が切れる。static に保持する
		static const std::string title = IMGUI::ToUTF8("アウトライナー");
		return title.c_str();
	}
	void Draw(_In_ EditorContext& ctx) override;

private:
	/// @brief シーンの情報を描画する
	void DrawSceneInfo(_In_ Scene& scene);

	/// @brief エンティティリストを描画する
	void DrawEntityList(_In_ EditorContext& ctx, _In_ World& world);

	/// @brief エンティティ 1 つとその子を再帰的に描画する
	void DrawEntityNode(_In_ EditorContext& ctx, _In_ World& world, _In_ Entity entity);

	bool MatchesFilter(_In_ World& world,_In_ Entity entity)const;

	bool IsFiltering() const;

	int m_ComponentFilter = -1;

	std::array<char, 64> m_EntityFilter{};
};
