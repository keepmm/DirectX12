/*!*************************************************************
 * \file   MmdPlayerPanel.cpp
 * \brief  MMD 再生コントローラー
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "MmdPlayerPanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "Components.hpp"
#include "MmdPlayerUI.hpp"


void MmdPlayerPanel::Draw(EditorContext& ctx)
{
	World* w = ctx.world();
	if (w == nullptr) return;
	World& world = *w;

	Entity target = INVALID_ENTITY;

	if (ctx.selectedEntity != INVALID_ENTITY &&
		world.IsEntityAlive(ctx.selectedEntity) &&
		world.HasComponent<AnimatorComponent>(ctx.selectedEntity))
	{
		target = ctx.selectedEntity;
	}
	else
	{
		world.Each<AnimatorComponent>([&](Entity e, AnimatorComponent&)
			{
				if (target == INVALID_ENTITY) target = e;
			});
	}

	if (target == INVALID_ENTITY)
	{
		ImGui::TextDisabled(u8("Animatorを持つエンティティがありません"));
		return;
	}

	DrawMmdPlayerControls(world, world.GetComponent<AnimatorComponent>(target));
}
