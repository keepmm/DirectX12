/*****************************************************************//**
 * \file   InspectorWindow.cpp
 * \brief  肥大化したEditorWindow.cppを分割するためのファイル
 * 
 * 作成者 keeep
 * 作成日 2026/6/20
 * 更新履歴 6.20 分割
 *			6.26 InspectorのAddComponentボタンの挙動を修正
 * *********************************************************************/
#include "EditorWindow.hpp"
#include "SceneSerializer.hpp"
#include "Components.hpp"
#include "PrefabLibrary.hpp"
#include "imguiinit.hpp"
#include <filesystem>
#include <cstdio>
#include "RuntimeScene.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include <Psapi.h>
#include "IconLibrary.hpp"
#include "ModelLoader.hpp"
#include "ImGuizmo.h"
#include "PlayState.hpp"
#include <shellapi.h>
#include "RegisterScript.hpp"
#include "ScriptHost.hpp"	
#include "ScriptField.hpp"
#include <functional>
#include <commdlg.h>
#include "json.hpp"
#include "ComponentRegistry.hpp"

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "psapi.lib")

void EditorWindow::DrawInspector(World& world, Scene* scene)
{
	if (!world.IsEntityAlive(m_SelectedEntity))
	{
		m_SelectedEntity = INVALID_ENTITY;
	}

	if (m_SelectedEntity == INVALID_ENTITY)
	{
		ImGui::Text(u8("エンティティを選択してください"));
		return;
	}

	ImGui::Text(u8("詳細情報"));
	ImGui::Separator();
	ImGui::Text("Entity ID: %u", m_SelectedEntity);
	ImGui::Separator();

	// ---- Name Component ---- //
	if (ImGui::CollapsingHeader(u8("Name Component"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (world.HasComponent<NameComponent>(m_SelectedEntity))
		{
			char nameBuffer[256];
			memcpy(nameBuffer, world.GetComponent<NameComponent>(m_SelectedEntity).name.c_str(), sizeof(nameBuffer));
			auto& nameComp = world.GetComponent<NameComponent>(m_SelectedEntity);
			if (ImGui::InputText(u8("##NameInput"), nameBuffer, sizeof(nameBuffer)))
			{
				// 入力された名前が空でないことを確認
				if (!nameBuffer[0] == '\0')
				{
					nameComp.name = std::string(nameBuffer);
				}
				else
				{
					nameComp.name = "Entity " + std::to_string(m_SelectedEntity);
				}
			}
			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##NameComponent")))
			{
				world.DeleteComponent<NameComponent>(m_SelectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##NameComponent")))
			{
				world.AddComponent<NameComponent>(m_SelectedEntity, NameComponent{ "Entity " });
			}
		}
	}

	// ---- Transform Component ---- //
	if (ImGui::CollapsingHeader(u8("Transform Component"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (world.HasComponent<TransformComponent>(m_SelectedEntity))
		{
			auto& transform = world.GetComponent<TransformComponent>(m_SelectedEntity);

			bool dirty = false;
			dirty |= ImGui::DragFloat3(u8("位置##Pos"), &transform.position.x, 0.1f);
			float3 euler = transform.EulerAngles;
			if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
			{
				transform.EulerAngles = euler;
				transform.ApplyEuler();
			}
			dirty |= ImGui::DragFloat3(u8("スケール##Scale"), &transform.scale.x, 0.1f);

			if (dirty)
			{
				transform.RebuildWorld();

				// コライダーの当たり判定も更新
				if (world.HasComponent<ColliderComponent>(m_SelectedEntity))
				{
					auto& collider = world.GetComponent<ColliderComponent>(m_SelectedEntity);

					if (collider.shapeType == ColliderComponent::ShapeType::Box)
					{
						collider.size = transform.scale;
					}
					else if (collider.shapeType == ColliderComponent::ShapeType::Sphere)
					{
						collider.radius = std::max(transform.scale.x, std::max(transform.scale.y, transform.scale.z)) * 0.5f;
					}

					// PhysicsWorldに反映
					if (scene && world.HasComponent<RigidBodyComponent>(m_SelectedEntity))
					{
						auto* physicsWorld = scene->GetPhysicsWorld();
						if (physicsWorld)
						{
							const auto& rb = world.GetComponent<RigidBodyComponent>(m_SelectedEntity);
							physicsWorld->RemoveRigidbody(m_SelectedEntity);
							physicsWorld->AddRigidbody(m_SelectedEntity, rb, collider);
						}
					}
				}
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##TransformComponent")))
			{
				TransformComponent tr{};
				tr.RebuildWorld();
				world.AddComponent<TransformComponent>(m_SelectedEntity, tr);
			}
		}
	}

	// ---- Mesh Component ---- //
	if (ImGui::CollapsingHeader(u8("Mesh Component")))
	{
		if (world.HasComponent<MeshComponent>(m_SelectedEntity))
		{
			auto& meshComp = world.GetComponent<MeshComponent>(m_SelectedEntity);
			ImGui::Text(u8("メッシュコンポーネント"));

			ImGui::Separator();

			// ファイルパスの設定
			char filepathBuffer[256];
			// 元のパスをコピー
			snprintf(filepathBuffer, sizeof(filepathBuffer), "%s", meshComp.FilePath.c_str());

			if (ImGui::InputText(u8("ファイルパス##MeshFilePath"), filepathBuffer, sizeof(filepathBuffer)))
			{
				meshComp.FilePath = filepathBuffer;
			}

			// -------------------------------------
			// アセットパネルからドロップを受け付け
			// -------------------------------------
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
				if (payload != nullptr)
				{
					meshComp.FilePath = std::string(static_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::Separator();


			ImGui::InputFloat(u8("スケール倍率##MeshScale"), &meshComp.scale);

			if (meshComp.scale <= 0.0f)
			{
				meshComp.scale = 0.01f;
			}

			if (ImGui::Button(u8("読み込み##MeshReload")))
			{
				auto result = ModelLoader::LoadFromFile(
					APP->GetDevice(),
					meshComp.FilePath,
					meshComp.scale);

				if (!meshComp.mesh)
				{
					// メッシュが未生成なら作る
					meshComp.mesh = std::make_shared<Mesh>();
					meshComp.mesh->CreateCube(APP->GetDevice());

					LOG->LogInfo(("メッシュを生成しました"));
				}

				if (result.mesh)
				{
					meshComp.mesh = result.mesh;
				}
				else
				{
					LOG->LogError(u8("メッシュの読み込みに失敗しました"));
				}
			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##MeshComponent")))
			{
				world.DeleteComponent<MeshComponent>(m_SelectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##MeshComponent")))
			{
				MeshComponent mesh{};
				world.AddComponent<MeshComponent>(m_SelectedEntity, mesh);

				// TransformとMaterialもないならついでに作る
				if (!world.HasComponent<TransformComponent>(m_SelectedEntity))
				{
					TransformComponent tr{};
					tr.RebuildWorld();
					world.AddComponent<TransformComponent>(m_SelectedEntity, tr);
				}

				if (!world.HasComponent<MaterialComponent>(m_SelectedEntity))
				{
					MaterialComponent material{};
					material.material = std::make_shared<Material>();
					material.material->Init();
					world.AddComponent<MaterialComponent>(m_SelectedEntity, material);
					LOG->LogInfo(("マテリアルを生成しました"));
				}
			}
		}
	}

	// ---- Material Component ---- //
	if (ImGui::CollapsingHeader(u8("Material Component")))
	{
		if (world.HasComponent<MaterialComponent>(m_SelectedEntity))
		{
			auto& materialComp = world.GetComponent<MaterialComponent>(m_SelectedEntity);

			// ファイルパスの設定
			char filepathBuffer[256];
			// 元のパスをコピー
			snprintf(filepathBuffer, sizeof(filepathBuffer), "%s", materialComp.FilePath.c_str());
			if (ImGui::InputText(u8("ファイルパス##MaterialFilePath"), filepathBuffer, sizeof(filepathBuffer)))
			{
				materialComp.FilePath = filepathBuffer;
			}

			// アセットパネルからドロップを受け付け
			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE");
				if (payload != nullptr)
				{
					materialComp.FilePath = std::string(static_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropTarget();
			}

			// 適用
			if (ImGui::Button(u8("適用##MaterialApply")))
			{
				// マテリアルが未生成なら作る
				if (!materialComp.material)
				{
					materialComp.material = std::make_shared<Material>();
					materialComp.material->Init();
				}

				std::wstring wpath = std::filesystem::path(materialComp.FilePath).wstring();
				if (!materialComp.material->SetTextureFromFile(wpath))
				{
					LOG->LogError("テクスチャの読み込みに失敗しました");
				}
				else
				{
					LOG->LogInfo(("テクスチャを適用しました"));
				}
			}

			const std::vector<std::string> names = APP->GetShaderNames();
			if (ImGui::BeginCombo(u8("Shader##Mat"), materialComp.shaderName.c_str()))
			{
				for (const auto& n : names)
				{
					bool selected = (materialComp.shaderName == n);
					if (ImGui::Selectable(n.c_str(), selected))
						materialComp.shaderName = n;   // shaderNameはコンポーネント共通なので配列側の変更は不要
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			// マルチマテリアルのスロット一覧（読み取り表示）
			Material* target = materialComp.material.get();
			if (!materialComp.materials.empty())
			{
				static int selectedSub = 0;   // 編集中のサブマテリアル
				const int count = (int)materialComp.materials.size();
				if (selectedSub >= count) selectedSub = 0;

				ImGui::Separator();
				ImGui::Text(u8("サブマテリアル: %d"), count);
				for (int i = 0; i < count; ++i)
				{
					ImGui::PushID(i);
					if (ImGui::Selectable(("Material " + std::to_string(i)).c_str(),
						selectedSub == i))
						selectedSub = i;
					ImGui::PopID();
				}
				if (materialComp.materials[selectedSub])
					// 選択中のサブマテリアルをtargetに設定
					target = materialComp.materials[selectedSub].get();
			}

			// --- マテリアル質感パラメータ ---
			if (target)
			{
				ImGui::Separator();
				ImGui::Text(u8("マテリアル質感パラメータ"));

				if (materialComp.shaderName == "PBR")
				{
					ImGui::SliderFloat(u8("Roughness##Mat"), &target->roughness, 0.0f, 1.0f);
					ImGui::SliderFloat(u8("Metallic##Mat"), &target->metallic, 0.0f, 1.0f);
					ImGui::ColorEdit4(u8("RimColor##Mat"), &target->rimColor.x);
				}
				if (materialComp.shaderName == "Rim")
				{
					ImGui::ColorEdit4(u8("RimColor##Mat"), &target->rimColor.x);
				}

				if (materialComp.shaderName == "Fresnel")
				{
					ImGui::ColorEdit4(u8("RimColor##Mat"), &target->rimColor.x);
					ImGui::SliderFloat(u8("Roughness##Mat"), &target->roughness, 0.0f, 1.0f);
				}

				if (materialComp.shaderName == "Dissolve")
				{
					ImGui::ColorEdit4(u8("RimColor##Mat"), &target->rimColor.x);
					ImGui::SliderFloat(u8("Roughness##Mat"), &target->roughness, 0.0f, 1.0f);
					ImGui::SliderFloat(u8("Metallic##Mat"), &target->metallic, 0.0f, 1.0f);
				}

				if (materialComp.shaderName == "BlinnPhong")
				{
					ImGui::SliderFloat(u8("Roughness##Mat"), &target->roughness, 0.0f, 1.0f);
					ImGui::SliderFloat(u8("Metallic##Mat"), &target->metallic, 0.0f, 1.0f);
				}
			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##MaterialComponent")))
			{
				world.DeleteComponent<MaterialComponent>(m_SelectedEntity);
			}

			// マテリアルの状態表示
			ImGui::Separator();
			if (materialComp.material)
			{
				ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), u8("マテリアル初期化済み"));
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8("マテリアル未初期化"));
			}

			// トゥーンランプテクスチャ
			char toonRampBuffer[256];
			snprintf(toonRampBuffer, sizeof(toonRampBuffer), "%s", materialComp.RampFilePath.c_str());
			if (ImGui::InputText(u8("トゥーンランプテクスチャ##ToonRampFilePath"), toonRampBuffer, sizeof(toonRampBuffer)))
			{
				materialComp.RampFilePath = toonRampBuffer;
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_TEXTURE");
				if (payload != nullptr)
				{
					materialComp.RampFilePath = static_cast<const char*>(payload->Data);
				}
				ImGui::EndDragDropTarget();
			}

			if (ImGui::Button(u8("ランプ適用##MaterialRampApply")))
			{
				if (materialComp.material)
				{
					std::wstring wpath = std::filesystem::path(materialComp.RampFilePath).wstring();
					if (!materialComp.material->SetToonRampTexture(wpath))
					{
						LOG->LogError(u8("ランプテクスチャの読み込みに失敗しました"));
					}
				}
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##MaterialComponent")))
			{
				MaterialComponent material{};
				world.AddComponent<MaterialComponent>(m_SelectedEntity, material);
			}
		}
	}

	for(const auto& compMeta : ComponentRegistry::All())
	{
		compMeta.draw(world, m_SelectedEntity);
	}

	// ---- Script Component ---- //
	if (world.HasComponent<ScriptComponent>(m_SelectedEntity))
	{
		if (ImGui::CollapsingHeader(u8("Script Component")))
		{
			auto& sc = world.GetComponent<ScriptComponent>(m_SelectedEntity);

			// 追加済みスクリプト一覧
			int removeIdx = -1;
			for (int i = 0; i < (int)sc.scriptNames.size(); i++)
			{
				const std::string& name = sc.scriptNames[i];
				ImGui::PushID(i);

				ImGui::Text("%s", name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("Remove")) removeIdx = i;

				// このスクリプト1個分だけ描画（二重ループ解消）
				auto& descs = sc.fieldDescs[name];
				auto& vals = sc.values[name];
				for (auto& d : descs)
				{
					FieldValue& v = vals[d.name];
					v.type = d.type;
					switch (d.type)
					{
					case FieldType::Int:    ImGui::DragInt(d.name.c_str(), &v.i); break;
					case FieldType::Float:  ImGui::DragFloat(d.name.c_str(), &v.f[0]); break;
					case FieldType::Float2: ImGui::DragFloat2(d.name.c_str(), v.f); break;
					case FieldType::Float3:
					case FieldType::Vector3:ImGui::DragFloat3(d.name.c_str(), v.f); break;
					case FieldType::Color:  ImGui::ColorEdit3(d.name.c_str(), v.f); break;
					case FieldType::Float4:
					case FieldType::Vector4:ImGui::DragFloat4(d.name.c_str(), v.f); break;
					case FieldType::Bool:   ImGui::Checkbox(d.name.c_str(), &v.b); break;
					case FieldType::String: /* InputText: v.s を char buf に橋渡し */ break;
					case FieldType::Entity:
					{
						Entity cur = (Entity)v.i;
						std::string label = "(None)";
						if (cur != INVALID_ENTITY)
						{
							label = world.HasComponent<NameComponent>(cur)
								? world.GetComponent<NameComponent>(cur).name + " (" + std::to_string(cur) + ")"
								: "Entity " + std::to_string(cur);
						}

						if (ImGui::BeginCombo(d.name.c_str(), label.c_str()))
						{
							if (ImGui::Selectable("(None)", cur == INVALID_ENTITY))
								v.i = (int)INVALID_ENTITY;

							// NameComponentを持つ全オブジェクトを名前付きで列挙
							world.Each<NameComponent>([&](Entity e, NameComponent& nm)
								{
									std::string n = nm.name + " (" + std::to_string(e) + ")";
									if (ImGui::Selectable(n.c_str(), e == cur))
										v.i = (int)e;
								});
							ImGui::EndCombo();
						}
						break;
					}
					}
				}

				ImGui::PopID();
			}
			if (removeIdx >= 0)
			{
				const std::string nm = sc.scriptNames[removeIdx];
				sc.scriptNames.erase(sc.scriptNames.begin() + removeIdx);
				sc.fieldDescs.erase(nm);
				sc.values.erase(nm);
			}

			ImGui::Separator();

			// Unity の Add Script ボタン
			if (ImGui::Button(u8("Add Script"), ImVec2(-1, 0)))
			{
				m_ScriptSerachBuffer[0] = '\0';
				m_FocusScriptSearch = true;
				ImGui::OpenPopup("ScriptSearchPopup");
			}

			if (ImGui::BeginPopup("ScriptSearchPopup"))
			{
				if (m_FocusScriptSearch)
				{
					ImGui::SetKeyboardFocusHere();
					m_FocusScriptSearch = false;
				}
				ImGui::SetNextItemWidth(240.0f);
				ImGui::InputTextWithHint("##ScriptSearch", u8("検索..."),
					m_ScriptSerachBuffer, sizeof(m_ScriptSerachBuffer));
				ImGui::Separator();

				const auto& names = ScriptHost::GetScriptNames();
				bool any = false;
				for (const auto& n : names)
				{
					// フィルタリング
					if (m_ScriptSerachBuffer[0] != '\0')
					{
						std::string lower = n, query = m_ScriptSerachBuffer;
						std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
						std::transform(query.begin(), query.end(), query.begin(), ::tolower);
						if (lower.find(query) == std::string::npos) continue;
					}

					bool alreadyAdded = std::find(sc.scriptNames.begin(), sc.scriptNames.end(), n) != sc.scriptNames.end();
					if (alreadyAdded) ImGui::BeginDisabled();
					if (ImGui::Selectable(n.c_str()))
					{
						sc.scriptNames.push_back(n);
						ImGui::CloseCurrentPopup();
					}
					if (alreadyAdded) ImGui::EndDisabled();
					any = true;
				}
				if (!any)
					ImGui::TextDisabled(u8("見つかりません"));

				ImGui::EndPopup();
			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##ScriptComponent")))
				world.DeleteComponent<ScriptComponent>(m_SelectedEntity);
		}
	}
	else
	{
		if (ImGui::Button(u8("Add Component##ScriptComponent")))
			world.AddComponent<ScriptComponent>(m_SelectedEntity, ScriptComponent{});
	}

	DrawAddComponentPopup(world, m_SelectedEntity);
}

void EditorWindow::DrawAddComponentPopup(World& world, Entity entity)
{
	ImGui::Separator();
	if (ImGui::Button(u8("Add Component"), ImVec2(-1, 0)))
	{
		m_AddCompSearchBuffer[0] = '\0';
		m_FocusAddCompSearch = true;
		ImGui::OpenPopup("AddComponentPopup");
	}
	if (!ImGui::BeginPopup("AddComponentPopup")) return;

	if (m_FocusAddCompSearch) { ImGui::SetKeyboardFocusHere(); m_FocusAddCompSearch = false; }
	ImGui::SetNextItemWidth(260.0f);
	ImGui::InputTextWithHint("##acsearch", u8("検索..."), m_AddCompSearchBuffer, sizeof(m_AddCompSearchBuffer));
	ImGui::Separator();

	auto match = [&](const std::string& s)
		{
			if (m_AddCompSearchBuffer[0] == '\0') return true;
			std::string a = s, q = m_AddCompSearchBuffer;
			std::transform(a.begin(), a.end(), a.begin(), ::tolower);
			std::transform(q.begin(), q.end(), q.begin(), ::tolower);
			return a.find(q) != std::string::npos;
		};

	// コンポーネント
	for (const auto& c : ComponentRegistry::All())
	{
		if (!match(c.name)) continue;
		bool has = c.has(world, entity);
		if (has) ImGui::BeginDisabled();
		if (ImGui::Selectable(c.name.c_str())) { c.add(world, entity); ImGui::CloseCurrentPopup(); }
		if (has) ImGui::EndDisabled();
	}

	// スクリプト
	ImGui::SeparatorText("Scripts");
	ImGui::Text("Open = %d names =%d", (int)ScriptHost::isOpen(), (int)ScriptHost::GetScriptNames().size());
	for (const auto& n : ScriptHost::GetScriptNames())
	{
		if (!match(n)) continue;
		bool added = false;
		if (world.HasComponent<ScriptComponent>(entity))
		{
			auto& sc = world.GetComponent<ScriptComponent>(entity);
			added = std::find(sc.scriptNames.begin(), sc.scriptNames.end(), n) != sc.scriptNames.end();
		}
		if (added) ImGui::BeginDisabled();
		if (ImGui::Selectable((n + "##sc").c_str()))
		{
			if (!world.HasComponent<ScriptComponent>(entity))
				world.AddComponent<ScriptComponent>(entity, ScriptComponent{});
			world.GetComponent<ScriptComponent>(entity).scriptNames.push_back(n);
			ImGui::CloseCurrentPopup();
		}
		if (added) ImGui::EndDisabled();
	}
	ImGui::EndPopup();
}
