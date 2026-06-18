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
			auto& nameComp = world.GetComponent<NameComponent>(m_SelectedEntity);
			if (ImGui::InputText(u8("##NameInput"), nameComp.name.data(), nameComp.name.size() + 1))
			{
				// 入力された名前が空でないことを確認
				if (!nameComp.name.empty())
				{
					nameComp.name = std::string(nameComp.name.data());
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
			dirty |= ImGui::DragFloat4(u8("回転(クォータニオン)##Rot"), &transform.rotation.x, 0.1f);
			dirty |= ImGui::DragFloat3(u8("スケール##Scale"), &transform.scale.x, 0.1f);

			if (dirty)
			{
				transform.MarkDirty();

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
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
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


			ImGui::Checkbox(u8("デフォルト以外のPixelShaderを使う##Mat"), &materialComp.usePixelShader);

			const char* shaderItems[] = { "BASIC", "TOON" };
			int psIndex = static_cast<int>(materialComp.pixelshader);
			if (ImGui::Combo(u8("PixelShader##Mat"), &psIndex, shaderItems, IM_ARRAYSIZE(shaderItems)))
			{
				materialComp.pixelshader = static_cast<E_PIXEL_SHADER>(psIndex);
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
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
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

	// ---- RigidBody Component ---- //
	if (ImGui::CollapsingHeader(u8("RigidBody Component")))
	{
		if (world.HasComponent<RigidBodyComponent>(m_SelectedEntity))
		{
			auto& rigidBodyComp = world.GetComponent<RigidBodyComponent>(m_SelectedEntity);
			ImGui::Text(u8("RigidBody Component"));
			ImGui::Separator();

			float mass = rigidBodyComp.mass;
			if (ImGui::DragFloat(u8("質量##RigidBody"), &mass, 0.1f, 0.0f, 1000.0f))
			{
				rigidBodyComp.mass = mass;

				// 質量が0以下にならないように
				if (rigidBodyComp.mass < 0.01f && !rigidBodyComp.isStatic)
				{
					rigidBodyComp.mass = 0.01f;
				}
			}

			// ツールチップ
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text(u8("オブジェクトの質量"));
				ImGui::EndTooltip();
			}

			// ボディタイプ選択
			ImGui::Separator();
			ImGui::Text(u8("ボディタイプ"));

			bool isKinematic = rigidBodyComp.isKinematic;
			bool isStatic = rigidBodyComp.isStatic;
			bool isDynamic = !isKinematic && !isStatic;
			bool useGravity = rigidBodyComp.useGravity;

			if (ImGui::Button(u8("重力のオン/オフ##RigidBodyGravity")))
			{
				rigidBodyComp.useGravity = !rigidBodyComp.useGravity;
			}
			ImGui::Separator();

			if (ImGui::RadioButton(u8("Dynamic##RigidBodyType"), isDynamic))
			{
				rigidBodyComp.isStatic = false;
				rigidBodyComp.isKinematic = false;
			}

			ImGui::SameLine();
			if (ImGui::RadioButton(u8("Kinematic##RigidBodyType"), isKinematic))
			{
				rigidBodyComp.isKinematic = true;
				rigidBodyComp.isStatic = false;
			}

			ImGui::SameLine();
			if (ImGui::RadioButton(u8("Static##RigidBodyType"), isStatic))
			{
				rigidBodyComp.isStatic = true;
				rigidBodyComp.isKinematic = false;
			}

			// アクター情報
			ImGui::Separator();
			if (rigidBodyComp.actor)
			{
				ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), u8("アクター初期化済み"));
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8("アクター未初期化"));
			}

			// 削除処理
			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##RigidBodyComponent")))
			{
				world.DeleteComponent<RigidBodyComponent>(m_SelectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##RigidBodyComponent")))
			{
				RigidBodyComponent rigidBody{};
				world.AddComponent<RigidBodyComponent>(m_SelectedEntity, rigidBody);
			}
		}
	}

	// ---- Collider Component ---- //
	if (ImGui::CollapsingHeader(u8("Collider Component")))
	{
		if (world.HasComponent<ColliderComponent>(m_SelectedEntity))
		{
			auto& colliderComp = world.GetComponent<ColliderComponent>(m_SelectedEntity);
			ImGui::Text(u8("コライダー設定"));
			ImGui::Separator();

			// シェイプタイプ選択
			ImGui::Text(u8("形状"));
			int shapeIndex = static_cast<int>(colliderComp.shapeType);
			const char* shapeItems[] = { "Box", "Sphere", "Capsule", "Mesh" };
			if (ImGui::Combo(u8("形状##ColliderShape"), &shapeIndex, shapeItems, IM_ARRAYSIZE(shapeItems)))
			{
				colliderComp.shapeType = static_cast<ColliderComponent::ShapeType>(shapeIndex);
			}

			ImGui::Separator();
			ImGui::Text(u8("物理パラメータ"));

			// サイズ設定
			if (colliderComp.shapeType == ColliderComponent::ShapeType::Box)
			{
				ImGui::DragFloat3(u8("サイズ##ColliderSize"), &colliderComp.size.x, 0.1f, 0.01f, 100.0f);
			}
			else if (colliderComp.shapeType == ColliderComponent::ShapeType::Sphere)
			{
				ImGui::DragFloat(u8("半径##ColliderRadius"), &colliderComp.radius, 0.1f, 0.01f, 100.0f);
			}
			else if (colliderComp.shapeType == ColliderComponent::ShapeType::Capsule)
			{
				ImGui::DragFloat(u8("半径##ColliderRadius"), &colliderComp.radius, 0.1f, 0.01f, 100.0f);
			}
			// 共通パラメータ
			ImGui::Separator();
			ImGui::DragFloat(u8("摩擦係数##ColliderFriction"), &colliderComp.friction, 0.05f, 0.0f, 1.0f);

			// ツールチップ
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text(u8("0~1: 小さいほど滑りやすい"));
				ImGui::EndTooltip();
			}

			ImGui::DragFloat(u8("反発係数##ColliderRestitution"), &colliderComp.restitution, 0.05f, 0.0f, 1.0f);

			// ツールチップ
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text(u8("0~1: 高いほど跳ねやすい"));
				ImGui::EndTooltip();
			}

			// シェイプ情報表示
			ImGui::Separator();
			if (colliderComp.shape)
			{
				ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), u8("シェイプ初期化済み"));
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), u8("シェイプ未初期化"));
			}

			// デバッグ描画オプション
			ImGui::Separator();
			ImGui::Text(u8("デバッグ表示"));

			ImGui::Checkbox(u8("当たり判定を表示##DebugCollider"), &m_ShowColliderDebug);

			if (m_ShowColliderDebug && scene && world.HasComponent<TransformComponent>(m_SelectedEntity))
			{
				const auto& transform = world.GetComponent<TransformComponent>(m_SelectedEntity);
				DrawColliderDebug(colliderComp, transform);

				// デバッグライン描画
				auto* runtimeScene = dynamic_cast<RuntimeScene*>(scene);
				if (runtimeScene)
				{
					runtimeScene->ClearDebugLines();
					for (const auto& line : m_DebugLines)
					{
						runtimeScene->AddDebugLine(line.start, line.end, line.color);
					}
				}
			}
			else if (scene)
			{
				auto* runtimeScene = dynamic_cast<RuntimeScene*>(scene);
				if (runtimeScene)
				{
					runtimeScene->ClearDebugLines();
				}
			}

			// 削除処理
			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##ColliderComponent")))
			{
				world.DeleteComponent<ColliderComponent>(m_SelectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##ColliderComponent")))
			{
				ColliderComponent collider{};
				if (world.HasComponent<TransformComponent>(m_SelectedEntity))
				{
					const auto& transform = world.GetComponent<TransformComponent>(m_SelectedEntity);
					collider.size = transform.scale;
				}
				else
				{
					collider.size = float3(1.0f, 1.0f, 1.0f);
				}
				collider.shapeType = ColliderComponent::ShapeType::Box;
				collider.radius = 0.5f;
				collider.friction = 0.5f;
				collider.restitution = 0.5f;
				collider.density = 1.0f;
				world.AddComponent<ColliderComponent>(m_SelectedEntity, collider);

				// PhysicsWorldに同期
				if (scene && world.HasComponent<RigidBodyComponent>(m_SelectedEntity))
				{
					auto* physicsWolrd = scene->GetPhysicsWorld();
					if (physicsWolrd)
					{
						const auto& rb = world.GetComponent<RigidBodyComponent>(m_SelectedEntity);
						physicsWolrd->AddRigidbody(m_SelectedEntity, rb, collider);
					}
				}
			}
		}
	}

	// ---- Spin Component ---- //
	if (ImGui::CollapsingHeader(u8("Spin Component")))
	{
		if (world.HasComponent<SpinComponent>(m_SelectedEntity))
		{


			auto& spinComp = world.GetComponent<SpinComponent>(m_SelectedEntity);
			ImGui::Text(u8("スピンコンポーネント"));

			ImGui::DragFloat(u8("角度##Spin"), &spinComp.angle, 0.1f, 0.0f, 360.0f);
			ImGui::DragFloat(u8("速度##Spin"), &spinComp.speed, 0.1f, 0.0f, 100.0f);

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##SpinComponent")))
			{
				world.DeleteComponent<SpinComponent>(m_SelectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##SpinComponent")))
			{
				SpinComponent spin{};
				world.AddComponent<SpinComponent>(m_SelectedEntity, spin);
			}
		}
	}

	// ---- Light Component ---- //
	if (ImGui::CollapsingHeader(u8("Light Component"), ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (world.HasComponent<LightComponent>(m_SelectedEntity))
		{
			auto& lightComp = world.GetComponent<LightComponent>(m_SelectedEntity);

			const char* typeItems[] = { "Directional", "Point", "Spot" };
			int typeIndex = static_cast<int>(lightComp.type);
			if (ImGui::Combo(u8("タイプ##Light"), &typeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
			{
				lightComp.type = static_cast<LightComponent::LightType>(typeIndex);
			}

			ImGui::ColorEdit4(u8("カラー##Light"), &lightComp.color.x);
			ImGui::ColorEdit4(u8("環境色##Light"), &lightComp.ambientColor.x);
			ImGui::DragFloat(u8("強度##Light"), &lightComp.intensity, 0.1f, 0.0f, 100.0f);
			ImGui::DragFloat3(u8("方向##Light"), &lightComp.direction.x, 0.1f, -1.0f, 1.0f);
			ImGui::DragFloat(u8("範囲##Light"), &lightComp.range, 0.1f, 0.0f, 100.0f);
			ImGui::DragFloat(u8("スポットアングル##Light"), &lightComp.spotAngle, 0.1f, 0.0f, 100.0f);
			ImGui::Checkbox(u8("有効##Light"), &lightComp.isActive);
			ImGui::Checkbox(u8("シャドウ投影##Light"), &lightComp.castShadows);

			ImGui::Separator();

			// ------------//
			//	ライト可視化 //
			// ------------//
			ImGui::Separator();
			if (ImGui::Checkbox(u8("ライトの可視化##LightDebug"), &lightComp.isShow))
			{

			}

			ImGui::Separator();
			if (ImGui::SmallButton(u8("Remove##LightComponent")))
			{
				world.DeleteComponent<LightComponent>(m_SelectedEntity);
			}
		}
		else
		{
			if (ImGui::Button(u8("Add Component##LightComponent")))
			{
				LightComponent light{};
				world.AddComponent<LightComponent>(m_SelectedEntity, light);
			}
		}
	}

	// Inspector のどこか（コンポーネント一覧の下など）
	if (ImGui::Button(u8("Add Component...")))
	{
		m_AddCompSearchBuffer[0] = '\0';          // 検索クリア
		m_FocusAddCompSearch = true;            // 次フレームでフォーカス
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		// 検索ボックス（開いた直後に自動フォーカス）
		if (m_FocusAddCompSearch) { ImGui::SetKeyboardFocusHere(); m_FocusAddCompSearch = false; }
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##AddCompSearch", u8("検索..."),
			m_AddCompSearchBuffer, sizeof(m_AddCompSearchBuffer));
		ImGui::Separator();

		// 大文字小文字を無視した部分一致
		auto match = [&](const std::string& s) -> bool {
			if (m_AddCompSearchBuffer[0] == '\0') return true;
			std::string a = s, b = m_AddCompSearchBuffer;
			std::transform(a.begin(), a.end(), a.begin(), ::tolower);
			std::transform(b.begin(), b.end(), b.begin(), ::tolower);
			return a.find(b) != std::string::npos;
			};

		// --- スクリプト（registry から） ---
		for (const auto& name : RegisterScript::Get().Names())
		{
			if (match(name) && ImGui::Selectable(name.c_str()))
			{
				RegisterScript::Get().Attach(name, world, m_SelectedEntity);
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
}

