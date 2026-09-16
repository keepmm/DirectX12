/*!*************************************************************
 * \file   HierarchyPanel.cpp
 * \brief  アウトライナー(シーン情報 + エンティティ一覧)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "HierarchyPanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "DirectX.hpp"
#include "Components.hpp"
#include "EntityFactory.hpp"
#include "RenderContext.hpp"
#include "UndoHistory.hpp"
#include <functional>


static Entity GetParent(World& world, Entity e)
{
	if (world.HasComponent<TransformComponent>(e))
		return world.GetComponent<TransformComponent>(e).parent;
	if (world.HasComponent<RectTransformComponent>(e))
		return world.GetComponent<RectTransformComponent>(e).parent;
	return INVALID_ENTITY;
}

static void SetParent(World& world, Entity child, Entity parent)
{
	if (world.HasComponent<TransformComponent>(child))
		world.GetComponent<TransformComponent>(child).parent = parent;
	else if (world.HasComponent<RectTransformComponent>(child))
		world.GetComponent<RectTransformComponent>(child).parent = parent;
}

static void SetParentKeepWorld(World& world, Entity child, Entity newParent)
{
	using namespace DirectX;
	if (!world.HasComponent<TransformComponent>(child)) return;
	auto& ct = world.GetComponent<TransformComponent>(child);

	// 子の今のワールド行列（TransformSystem計算済み）
	XMMATRIX childWorld = XMLoadFloat4x4(&ct.world);

	if (newParent != INVALID_ENTITY && world.HasComponent<TransformComponent>(newParent))
	{
		auto& pt = world.GetComponent<TransformComponent>(newParent);
		XMMATRIX parentWorld = XMLoadFloat4x4(&pt.world);

		// 新ローカル = 子ワールド x 親ワールドの逆
		XMVECTOR det;
		XMMATRIX newLocal = childWorld * XMMatrixInverse(&det, parentWorld);

		XMVECTOR s, r, t;
		XMMatrixDecompose(&s, &r, &t, newLocal);
		XMStoreFloat3(&ct.position, t);
		XMStoreFloat4(&ct.rotation, r);
		XMStoreFloat3(&ct.scale, s);
	}
	ct.parent = newParent;
}

static bool IsAncestor(World& world, Entity maybeAncestor, Entity child)
{
	Entity cur = child;
	while (cur != INVALID_ENTITY && world.HasComponent<TransformComponent>(cur))
	{
		Entity p = world.GetComponent<TransformComponent>(cur).parent;
		if (p == maybeAncestor) return true;
		cur = p;
	}
	return false;
}

void HierarchyPanel::DrawSceneInfo(Scene& scene)
{
	ImGui::Text(u8("シーン: %s"), scene.GetSceneName().c_str());
	ImGui::Separator();
	ImGui::Checkbox("Deferred Rendering", &RenderSettings::Get().deferred);
	ImGui::Checkbox("Volumetric Light", &RenderSettings::Get().volumetric);
	ImGui::SliderFloat("IBL Intensity", &RenderSettings::Get().envIntensity, 0.0f, 2.0f);
	ImGui::Text("Lights: %u (volumetric %u)",
		APP->GetLastLightCount(), APP->GetLastVolumetricCount());
	ImGui::Separator();
}

void HierarchyPanel::DrawEntityList(EditorContext& ctx, World& world)
{
	ImGui::Text(u8("エンティティ一覧"));
	ImGui::InputText(u8("##FilterEntity"), m_EntityFilter.data(), m_EntityFilter.size());

	if (ImGui::BeginChild("EntityList", ImVec2(0.0f, 0.0f), true))
	{
		//   TransformもRectTransformも無いエンティティも拾う
		for (Entity e : world.GetEntities())
		{
			if (GetParent(world, e) == INVALID_ENTITY)   // ルートだけ
				DrawEntityNode(ctx, world, e);
		}

		// 余白へのドロップ＝ルートに戻す
		ImGui::Dummy(ImGui::GetContentRegionAvail());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
			{
				Entity child = *(const Entity*)p->Data;
				// 選択していない Entity を書き換えることがあるので、前後を明示的に記録する
				auto apply = [&]() { SetParent(world, child, INVALID_ENTITY); };
				if (ctx.history && ctx.activeScene) ctx.history->RecordEdit(*ctx.activeScene, child, apply);
				else apply();
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextWindow())
		{
			// 作成を Undo できるよう、増えた Entity を記録する
			// (UI Image / Text が Canvas を一緒に作る場合もまとめて 1 回で戻る)
			auto recordCreate = [&](const std::function<Entity()>& create)
				{
					const size_t before = world.GetEntities().size();
					ctx.selectedEntity = create();
					if (ctx.history && ctx.activeScene)
						ctx.history->RecordCreated(*ctx.activeScene, before);
				};

			if (ImGui::BeginMenu(u8("作成")))
			{
				if (ImGui::MenuItem(u8("空のエンティティ")))
				{
					recordCreate([&]()
						{
							static int entityCount = 1;
							Entity e = world.CreateEntity();
							world.AddComponent<NameComponent>(e, NameComponent{ "Entity " + std::to_string(entityCount++) });
							world.AddComponent<TransformComponent>(e, TransformComponent{});
							return e;
						});
				}

				if (ImGui::BeginMenu(u8("プリミティブ")))
				{
					if (ImGui::MenuItem(u8("球")))
						recordCreate([&]() { return EntityFactory::CreatePrimitive(world, kPrimitiveSphere); });
					if (ImGui::MenuItem(u8("立方体")))
						recordCreate([&]() { return EntityFactory::CreatePrimitive(world, kPrimitiveCube); });
					ImGui::EndMenu();
				}

				if (ImGui::BeginMenu("UI"))
				{
					if (ImGui::MenuItem("Image")) recordCreate([&]() { return EntityFactory::CreateImage(world); });
					if (ImGui::MenuItem("Text"))  recordCreate([&]() { return EntityFactory::CreateText(world); });
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem(u8("エンティティを削除")) && ctx.selectedEntity != INVALID_ENTITY)
			{
				if (ctx.history && ctx.activeScene)
				{
					// 取り外して履歴に保管する(Undo で同じ ID・同じ見た目のまま戻る)
					ctx.history->DeleteEntity(*ctx.activeScene, ctx.selectedEntity);
				}
				else
				{
					APP->WaitForGPUIdle();
					world.DestroyEntity(ctx.selectedEntity);
				}
				ctx.selectedEntity = INVALID_ENTITY;
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild(); 
}

void HierarchyPanel::DrawEntityNode(EditorContext& ctx, World& world, Entity entity)
{
	std::string label = world.HasComponent<NameComponent>(entity)
		? world.GetComponent<NameComponent>(entity).name
		: ("Entity " + std::to_string(entity));

	// 子を持っているか調べる
	bool hasChildren = false;
	world.Each<TransformComponent>([&](Entity e, TransformComponent& t) {
		if (t.parent == entity) hasChildren = true;
		});

	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (ctx.selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;
	if (!hasChildren)               flags |= ImGuiTreeNodeFlags_Leaf;  // 子無しは?を出さない

	ImGui::PushID((int)entity);
	bool open = ImGui::TreeNodeEx(label.c_str(), flags);

	// クリックで選択
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		ctx.selectedEntity = entity;

	// ドラッグソース
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("ENTITY", &entity, sizeof(Entity));
		ImGui::Text("%s", label.c_str());
		ImGui::EndDragDropSource();
	}
	// ドロップターゲット（この上にドロップ = この子になる）
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
		{
			Entity child = *(const Entity*)p->Data;
			if (child != entity && !IsAncestor(world, child, entity))
			{
				auto apply = [&]() { SetParentKeepWorld(world, child, entity); };
				if (ctx.history && ctx.activeScene) ctx.history->RecordEdit(*ctx.activeScene, child, apply);
				else apply();
			}
		}
		ImGui::EndDragDropTarget();
	}

	// 開いていれば子を再帰描画
	if (open)
	{
		world.Each<TransformComponent>([&](Entity e, TransformComponent& t) {
			if (t.parent == entity)
				DrawEntityNode(ctx, world, e);
			});
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void HierarchyPanel::Draw(EditorContext& ctx)
{
	if (ctx.activeScene == nullptr)
	{
		ImGui::Text(u8("アクティブなシーンがありません"));
		return;
	}

	World& world = ctx.activeScene->GetWorld();
	DrawSceneInfo(*ctx.activeScene);
	DrawEntityList(ctx, world);
}
