/*!*************************************************************
 * ile   LiveTimelinePanel.cpp
 * rief  ライブ演出タイムラインのエディタパネル
 *
 * 作成者 keeep
 * 作成日 2026/9/14
 * 更新履歴	9.14 engine のパネル分割に合わせて EditorWindow から移植
 * *********************************************************************/
#include "LiveTimelinePanel.hpp"

#include "imguiinit.hpp"
#include "Components.hpp"
#include "LiveTimelineUI.hpp"

void LiveTimelinePanel::Draw(EditorContext& ctx)
{
	// シーン切り替えの最中はアクティブシーンが一瞬 nullptr になる
	World* world = ctx.world();
	if (world == nullptr) return;

	DrawLiveTimelineEditor(*world, ctx.selectedEntity);
}
