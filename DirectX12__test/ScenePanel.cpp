/*!*************************************************************
 * \file   ScenePanel.cpp
 * \brief  シーン設定パネル(保存 / 読み込みと Prefab 配置)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "ScenePanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include <cstdio>
#include <filesystem>
#include "DirectX.hpp"
#include "SceneManager.hpp"
#include "SceneSerializer.hpp"
#include "PrefabLibrary.hpp"
#include "Project.hpp"
#include "Components.hpp"
#include "RuntimeScene.hpp"


void ScenePanel::Draw(EditorContext& ctx)
{
	SceneManager& sceneManager = ctx.sceneManager;
	ImGui::Text(u8("シーンコントロール"));
	ImGui::Separator();

	auto activeScene = sceneManager.GetActiveScene();
	if (!activeScene) return;

	// 初回はアクティブシーンの名前を入れておく(旧 EditorWindow のコンストラクタが行っていた)
	if (m_SceneRegisterName[0] == 0)
	{
		std::snprintf(m_SceneRegisterName.data(), m_SceneRegisterName.size(),
			"%s", activeScene->GetSceneName().c_str());
	}

	if (ImGui::InputText(u8("シーン名##SceneName"), m_SceneRegisterName.data(), m_SceneRegisterName.size()))
	{

	}

	if (ImGui::Button(u8("登録##RegisterScene")))
	{
		std::string name = m_SceneRegisterName.data();
		if (!name.empty())
		{
			sceneManager.RegisterScene(name);   // パスは規約から自動
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(u8("ロード##LoadScene")))
	{
		std::string name = m_SceneRegisterName.data();
		if (!name.empty())
		{
			sceneManager.RegisterScene(name);   // 未登録なら登録してから
			sceneManager.LoadScene(name);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(u8("保存##SaveScene")))
	{
		if (activeScene)
		{
			SceneSerializer::Save(*activeScene,
				SceneManager::ScenePathFromName(activeScene->GetSceneName()));
		}
	}

	ImGui::Separator();
	if (activeScene)
	{
		ImGui::Text(u8("スカイボックス: %s"), activeScene->GetSkyboxPath().c_str());
		ImGui::SameLine();
		if (ImGui::Button(u8("参照...##PickSkybox")))
		{
			std::wstring picked;
			if (OpenFileDialog(picked, L"HDR/Sky Texture\0*.hdr;*.dds;*.png;*.jpg\0All\0*.*\0"))
			{
				std::string p = WideToUtf8(picked);
				const size_t pos = p.find("Assets\\");
				if (pos != std::string::npos)
				{
					p = p.substr(pos);
					for (auto& c : p) if (c == '\\') c = '/';   // JSONでの見た目統一
				}
				if (auto* rs = dynamic_cast<RuntimeScene*>(activeScene))
					rs->SetSkybox(p);
			}
		}
	}

	ImGui::Separator();

	if (ImGui::Button(u8("World リセット##ResetWorld")))
	{
		activeScene->ResetWorld();
	}

	ImGui::SameLine();
	if (ImGui::Button(u8("PhysicsWorld リセット##ResetPhysicsWorld")))
	{
		activeScene->ResetPhysicsWorld();
	}

	ImGui::SameLine();
	if (ImGui::Button(u8("PhysicsWorld 初期化##InitPhysicsWorld")))
	{
		auto& physicsWorld = activeScene->EnsurePhysicsWorld();
		physicsWorld.Init();
	}

	ImGui::Separator();

	if (ImGui::Button(u8("ライト追加##AddLight")))
	{
		World& world = activeScene->GetWorld();
		Entity entity = world.CreateEntity();

		world.AddComponent<NameComponent>(entity, NameComponent{ "Light" });

		TransformComponent tr{};
		tr.position = float3(0.0f, 5.0f, 0.0f);
		tr.RebuildWorld();
		world.AddComponent<TransformComponent>(entity, tr);

		LightComponent light{};
		light.type = LightComponent::LightType::Point;
		world.AddComponent<LightComponent>(entity, light);

		ctx.selectedEntity = entity;
	}

	ImGui::Separator();
	DrawPrefabPanel(ctx, *activeScene, activeScene->GetWorld());
}

void ScenePanel::DrawPrefabPanel(EditorContext& ctx, Scene& scene, World& world)
{
	PrefabLibrary& library = PrefabLibrary::Get();
	auto prefabNames = library.GetPrefabNames();

	if (prefabNames.empty())
	{
		ImGui::Text(u8("Prefab が登録されていません"));
		return;
	}

	if (m_SelectedPrefab.empty())
	{
		m_SelectedPrefab = prefabNames.front();
	}

	if (ImGui::BeginCombo(u8("Prefab##PrefabSelect"), m_SelectedPrefab.c_str()))
	{
		for (const auto& name : prefabNames)
		{
			bool isSelected = (m_SelectedPrefab == name);
			if (ImGui::Selectable(name.c_str(), isSelected))
			{
				m_SelectedPrefab = name;
			}
			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	if (ImGui::Button(u8("インスタンス##Instantiate")))
	{
		Entity created = library.Instantiate(m_SelectedPrefab, scene, world);
		if (created != INVALID_ENTITY)
		{
			if (world.HasComponent<TransformComponent>(created))
			{
				auto& tr = world.GetComponent<TransformComponent>(created);
				tr.position = m_PrefabPosition;
				tr.RebuildWorld();
			}
			else
			{
				TransformComponent tr{};
				tr.position = m_PrefabPosition;
				tr.RebuildWorld();
				world.AddComponent<TransformComponent>(created, tr);
			}

			if (auto* physicsWorld = scene.GetPhysicsWorld())
			{
				if (world.HasComponent<RigidBodyComponent>(created) &&
					world.HasComponent<ColliderComponent>(created))
				{
					const auto& tr = world.GetComponent<TransformComponent>(created);
					physicsWorld->SetActorPose(created, tr.position, tr.rotation);
				}
			}

			ctx.selectedEntity = created;
		}
	}
}
