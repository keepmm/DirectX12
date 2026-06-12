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

#pragma comment(lib, "psapi.lib")

EditorWindow::EditorWindow(DirectXApp& app)
	: m_App(app)
{
	// ファイルパスの初期値を設定
	std::snprintf(m_SceneNameInput.data(), m_SceneNameInput.size(), "Assets/Scenes/SampleScene.json");
	std::snprintf(m_SceneRegisterName.data(), m_SceneRegisterName.size(), "SampleScene");
	std::snprintf(m_SceneRegisterPath.data(), m_SceneRegisterName.size(), "Assets/Scenes/SampleScene.json");

	// ゲーム画面用のレンダーテクスチャ初期化
	m_GameRenderTexture = std::make_unique<RenderTexture>();
	if (FAILED(m_GameRenderTexture->Init(m_App, 1280, 720)))
	{
		m_GameRenderTexture = nullptr;
		m_GameTextureHandleValid = false;
	}
	else
	{
		m_GameTextureHandleValid = true;
	}

	// エディタ用のレンダーテクスチャ初期化
	m_EditorRenderTexture = std::make_unique<RenderTexture>();
	if (FAILED(m_EditorRenderTexture->Init(m_App, 1280, 720)))
	{
		m_EditorRenderTexture = nullptr;
		m_EditorTextureHandleValid = false;
	}
	else
	{
		m_EditorTextureHandleValid = true;
	}
}

void EditorWindow::Draw(SceneManager& sceneManager)
{
	// 前フレームのデバッグlineをクリア

	// アクティブなシーンを取得
	Scene* activeScene = sceneManager.GetActiveScene();

	// メニューバー
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu(u8("ファイル")))
		{
			if (ImGui::MenuItem(u8("シーンを保存"), "Ctrl+S"))
			{
				if (activeScene)
				{
					SceneSerializer::Save(*activeScene, m_SceneRegisterPath.data());
				}
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(u8("ウィンドウ")))
		{
			ImGui::Checkbox(u8("アウトライナーを表示"), &m_ShowOutliner);
			ImGui::Checkbox(u8("ビューポートを表示"), &m_ShowViewport);
			ImGui::Checkbox(u8("プロパティを表示"), &m_ShowProperties);
			ImGui::Checkbox(u8("メモリ消費量を表示"), &m_ShowMemory);
			ImGui::Checkbox(u8("詳細を表示"), &m_ShowDetails);
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	// ドッキングスペースのセットアップ
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags dockWindowFlags = ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_MenuBar;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("DockSpace", nullptr, dockWindowFlags);
	ImGui::PopStyleVar(2);

	ImGuiID dockspaceID = ImGui::GetID("EditorDockSpace");
	ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	// ドッキングレイアウトの初期設定
	if (!m_DockLayout || ImGui::DockBuilderGetNode(dockspaceID) == nullptr)
	{
		ImGui::DockBuilderRemoveNode(dockspaceID);
		ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_None);
		ImGui::DockBuilderSetNodeSize(dockspaceID, viewport->WorkSize);

		ImGuiID dockMainID = dockspaceID;
		ImGuiID dockLeftID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Left, 0.15f, nullptr, &dockMainID);
		ImGuiID dockRightID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Right, 0.18f, nullptr, &dockMainID);
		ImGuiID dockBottomID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Down, 0.30f, nullptr, &dockMainID);

		ImGui::DockBuilderDockWindow(u8("アウトライナー"), dockLeftID);
		ImGui::DockBuilderDockWindow(u8("ゲーム画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("エディタ画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("プロパティパネル"), dockRightID);
		ImGui::DockBuilderDockWindow(u8("詳細パネル"), dockBottomID);

		ImGui::DockBuilderFinish(dockspaceID);
		m_DockLayout = true;
	}
	ImGui::End();

	// ---- アウトライナーパネル ---- //
	if (ImGui::Begin(u8("アウトライナー")) && m_ShowOutliner)
	{
		if (activeScene == nullptr)
		{
			ImGui::Text(u8("アクティブなシーンがありません"));
		}
		else
		{
			World& world = activeScene->GetWorld();
			DrawSceneInfo(*activeScene);
			DrawEntityList(world);
		}
	}
	ImGui::End();

	// ---- ビューポートパネル ---- //
	if (ImGui::Begin(u8("ゲーム画面")) && m_ShowViewport)
	{
		ImVec2 availableSize = ImGui::GetContentRegionAvail();

		// --------------------------------------------------------------------//
		//	ビューポートのサイズが変更された場合、レンダーテクスチャもリサイズ //
		// --------------------------------------------------------------------//
		const UINT newWidth = static_cast<UINT>(availableSize.x);
		const UINT newHeight = static_cast<UINT>(availableSize.y);
		if (m_GameRenderTexture && newWidth > 0 && newHeight > 0 &&
			newWidth != m_GameRenderTexture->GetWidth() || newHeight != m_GameRenderTexture->GetHeight())
		{
			// 古いレンダーテクスチャを解放
			m_App.WaitForGPUIdle();	// GPUが待機状態になるのを待つ
			m_GameRenderTexture->Init(m_App, newWidth, newHeight);
		}

		m_ViewportPos = ImGui::GetCursorScreenPos();
		m_ViewportSize = availableSize;

		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_GameRenderTexture && m_GameTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_GameRenderTexture->GetSRV().ptr),
				availableSize, ImVec2(0, 0), ImVec2(1, 1));

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload =
					ImGui::AcceptDragDropPayload("ASSET_MODEL");
				if(payload != nullptr && activeScene != nullptr)
				{
					// 運ばれてきたファイルパスを取り出す
					std::string modelpath(static_cast<const char*>(payload->Data));

					// とりあえず原点に
					float3 fragPosition = float3(0.0f, 0.0f, 0.0f);

					SpawnModelFromFile(activeScene->GetWorld(), modelpath, fragPosition);
				}
				ImGui::EndDragDropTarget();
			}
		}
		else
		{
			LOG->LogError("レンダーテクスチャが未初期化です");
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImU32 backgroundColor = ImGui::GetColorU32(ImGuiCol_WindowBg);
			drawList->AddRectFilled(
				m_ViewportPos,
				ImVec2(m_ViewportPos.x + m_ViewportSize.x, m_ViewportPos.y + m_ViewportSize.y),
				backgroundColor
			);
		}
	}
	ImGui::End();

	// ---- エディタ画面パネル ---- //
	if (ImGui::Begin(u8("エディタ画面")) && m_ShowViewport)
	{
		ImVec2 availableSize = ImGui::GetContentRegionAvail();
		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_EditorRenderTexture && m_EditorTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_EditorRenderTexture->GetSRV().ptr),
				availableSize, ImVec2(0, 0), ImVec2(1, 1));
		}
		else
		{
			LOG->LogError("エディタ用レンダーテクスチャが未初期化です");
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImU32 backgroundColor = ImGui::GetColorU32(ImGuiCol_WindowBg);
			drawList->AddRectFilled(
				m_ViewportPos,
				ImVec2(m_ViewportPos.x + m_ViewportSize.x, m_ViewportPos.y + m_ViewportSize.y),
				backgroundColor
			);
		}
	}
	ImGui::End();

	// ---- プロパティパネル（インスペクタ）---- //
	if (ImGui::Begin(u8("プロパティパネル")) && m_ShowProperties)
	{
		if (activeScene == nullptr)
		{
			ImGui::Text(u8("アクティブなシーンがありません"));
		}
		else
		{
			World& world = activeScene->GetWorld();
			DrawInspector(world,activeScene);
		}
	}
	ImGui::End();

	// ---- 詳細パネル ---- //
	if (ImGui::Begin(u8("詳細パネル")) && m_ShowDetails)
	{
		if (ImGui::BeginTabBar(u8("詳細パネルタブ")))
		{
			if (ImGui::BeginTabItem(u8("アセット")))
			{
				DrawAssetPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem(u8("シーン設定")))
			{
				if (activeScene)
				{
					DrawScenePanel(sceneManager);
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabBar(u8("メモリ消費量")))
			{
				if (m_ShowMemory)
				{
					DrawMemoryPanel();
				}
				ImGui::EndTabBar();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

void EditorWindow::DrawSceneInfo(Scene& scene)
{
	ImGui::Text(u8("シーン: %s"), scene.GetSceneName().c_str());
	ImGui::Separator();
}

void EditorWindow::DrawEntityList(World& world)
{
	ImGui::Text(u8("エンティティ一覧"));
	ImGui::InputText(u8("##FilterEntity"), m_EntityFilyer.data(), m_EntityFilyer.size());

	const std::string filtertext = m_EntityFilyer.data();

	if (ImGui::BeginChild("EntityList", ImVec2(0.0f, 0.0f), true))
	{
		for (Entity entity : world.GetEntities())
		{
			std::string label = "Entity " + std::to_string(entity);
			if (world.HasComponent<NameComponent>(entity))
			{
				label = world.GetComponent<NameComponent>(entity).name;
			}

			if (!filtertext.empty() && label.find(filtertext) == std::string::npos)
			{
				continue;
			}

			bool selected = (m_SelectedEntity == entity);
			if (ImGui::Selectable(label.c_str(), selected))
			{
				m_SelectedEntity = entity;
			}
		}
	}

	// -------------------------------------//
	//		Window内で右クリックで新規メニュー   //
	// -------------------------------------//
	if(ImGui::BeginPopupContextWindow())
	{
		if (ImGui::MenuItem(u8("エンティティを追加")))
		{
			static int entityCount = 1;
			auto name = "Entity " + std::to_string(entityCount++);

			Entity newEntity = world.CreateEntity();
			world.AddComponent<NameComponent>(newEntity, NameComponent{ name });
		}

		if (ImGui::MenuItem(u8("エンティティを削除")) && m_SelectedEntity != INVALID_ENTITY)
		{
			world.DestroyEntity(m_SelectedEntity);
		}
		ImGui::EndPopup();
	}
	ImGui::EndChild();
}

void EditorWindow::DrawInspector(World& world,Scene* scene)
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
			if (ImGui::BeginDragDropSource())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
				if (payload != nullptr)
				{
					meshComp.FilePath = std::string(static_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropSource();
			}

			ImGui::Separator();

			static float scale;
			
			ImGui::InputFloat(u8("スケール倍率##MeshScale"), &scale);

			if(scale <= 0.0f)
			{
				scale = 0.01f;
			}

			if (ImGui::Button(u8("読み込み##MeshReload")))
			{
				auto result = ModelLoader::LoadFromFile(
					APP->GetDevice(),
					meshComp.FilePath,
					scale);

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
			if (ImGui::BeginDragDropSource())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
				if (payload != nullptr)
				{
					materialComp.FilePath = std::string(static_cast<const char*>(payload->Data));
				}
				ImGui::EndDragDropSource();
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
				if(!materialComp.material->SetTextureFromFile(wpath))
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

			if(ImGui::Button(u8("重力のオン/オフ##RigidBodyGravity")))
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
				if(world.HasComponent<TransformComponent>(m_SelectedEntity))
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
}

void EditorWindow::DrawPrefabPanel(Scene& scene, World& world)
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

			m_SelectedEntity = created;
		}
	}
}

void EditorWindow::DrawAssetPanel()
{
	namespace fs = std::filesystem;
	const fs::path assetRoot = "Assets";

	if (!fs::exists(assetRoot))
	{
		ImGui::Text(u8("Assets フォルダが見つかりません"));
		return;
	}

	// 開いていたフォルダが消されていたらルートに戻す
	if (!fs::exists(m_CurrentAssetDir))
	{
		m_CurrentAssetDir = assetRoot.string();
	}

	// -------------------------//
	//		 ツールバー			//
	// -------------------------//

	// 「↑」ボタン: 親フォルダへ（ルートでは無効化）
	const bool atRoot = fs::equivalent(m_CurrentAssetDir, assetRoot);
	ImGui::BeginDisabled(atRoot);
	if (ImGui::Button(u8("↑##AssetUp")))
	{
		m_CurrentAssetDir = fs::path(m_CurrentAssetDir).parent_path().string();
	}
	ImGui::EndDisabled();

	// 現在のパスを表示
	ImGui::SameLine();
	ImGui::Text("%s", m_CurrentAssetDir.c_str());

	// セルサイズのスライダー
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat(u8("サイズ##AssetCell"), &m_AssetCellSize, 48.0f, 128.0f, "%.0f");

	ImGui::Separator();

	const float cellSize = m_AssetCellSize;
	const ImVec2 tileSize(cellSize, cellSize);

	if (ImGui::BeginChild("AssetList", ImVec2(0.0f, 0.0f), true))
	{
		const float windowRight =
			ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		const ImGuiStyle& style = ImGui::GetStyle();

		// ---------------------------------------//
		// フォルダ→ファイルの順に並べたいので分ける //
		// ---------------------------------------//
		std::vector<fs::directory_entry> folders;
		std::vector<fs::directory_entry> files;
		for (const auto& entry : fs::directory_iterator(m_CurrentAssetDir))
		{
			if (entry.is_directory())
			{
				folders.push_back(entry);
			}
			else
			{
				files.push_back(entry);
			}
		}

		std::string pendingDir;	// イテレーション中にm_CurrentAssetDirを書き換えないため

		// タイル1個を描く共通処理
		auto drawTile = [&](const fs::directory_entry& entry, bool isFolder)
			{
				const std::string name = entry.path().filename().string();
				const std::string fullPath = entry.path().string();
				const std::string ext = entry.path().extension().string();

				// -------------------------------------//
				// アイコンとフォールバック色を決める	//
				// -------------------------------------//
				std::wstring iconPath;
				const char* label = "FILE";
				ImVec4 color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);

				if (isFolder)
				{
					iconPath = L"Assets/Icons/Folder.png";
					label = "DIR";
					color = ImVec4(0.8f, 0.7f, 0.3f, 1.0f);
				}
				else if (ext == ".fbx" || ext == ".obj")
				{
					iconPath = L"Assets/Icons/Model.png";
					label = "3D";
					color = ImVec4(0.8f, 0.5f, 0.2f, 1.0f);
				}
				else if (ext == ".png" || ext == ".jpg" || ext == ".bmp")
				{
					iconPath = entry.path().wstring();	// 画像は自分自身をサムネイルに
					label = "IMG";
					color = ImVec4(0.3f, 0.7f, 0.3f, 1.0f);
				}
				else if (ext == ".json")
				{
					iconPath = L"Assets/Icons/Json.png";
					label = "JSON";
					color = ImVec4(0.3f, 0.5f, 0.8f, 1.0f);
				}
				else
				{
					iconPath = L"Assets/Icons/File.png";
				}

				const bool selected = (m_SelectedAsset == fullPath);
				if (selected)
				{
					color.x = std::min(color.x + 0.2f, 1.0f);
					color.y = std::min(color.y + 0.2f, 1.0f);
					color.z = std::min(color.z + 0.2f, 1.0f);
				}

				ImGui::PushID(fullPath.c_str());
				ImGui::BeginGroup();

				// ---- タイル本体 ---- //
				bool clicked = false;
				ImTextureID icon = IconLibrary::Get()->GetOrLoad(iconPath);
				if (icon != 0)
				{
					clicked = ImGui::ImageButton("##tile", icon, tileSize);
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Button, color);
					clicked = ImGui::Button(label, tileSize);
					ImGui::PopStyleColor();
				}

				if (clicked)
				{
					m_SelectedAsset = fullPath;
				}

				// フォルダはダブルクリックで中に入る
				if (isFolder &&
					ImGui::IsItemHovered() &&
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					pendingDir = fullPath;
				}

				// ---- ドラッグ元（ファイルのみ）---- //
				if (!isFolder && ImGui::BeginDragDropSource())
				{
					ImGui::SetDragDropPayload("ASSET_MODEL",
						fullPath.c_str(), fullPath.size() + 1);
					ImGui::Text("%s", name.c_str());
					ImGui::EndDragDropSource();
				}

				// ホバーでフルパス表示
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("%s", fullPath.c_str());
					ImGui::EndTooltip();
				}

				// ---- 名前（タイル幅で折り返し）---- //
				ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cellSize);
				ImGui::TextUnformatted(name.c_str());
				ImGui::PopTextWrapPos();

				ImGui::EndGroup();

				// ---- 折り返し ---- //
				const float lastTileRight = ImGui::GetItemRectMax().x;
				const float nextTileRight = lastTileRight + style.ItemSpacing.x + cellSize;
				if (nextTileRight < windowRight)
				{
					ImGui::SameLine();
				}

				ImGui::PopID();
			};

		// フォルダ → ファイルの順で描画
		for (const auto& f : folders) { drawTile(f, true); }
		for (const auto& f : files) { drawTile(f, false); }

		// ループ後にフォルダ移動を反映
		if (!pendingDir.empty())
		{
			m_CurrentAssetDir = pendingDir;
		}
	}
	ImGui::EndChild();
}

#pragma region MemoryInfo

constexpr double BYTES_TO_MB = 1.0 / (1024.0 * 1024.0);

SIZE_T GetPrivateWorkingSetBytes(HANDLE process)
{
	DWORD bufferSize = sizeof(PSAPI_WORKING_SET_INFORMATION) + sizeof(ULONG_PTR) * 4096;
	std::vector<unsigned char> buffer(bufferSize);

	for (;;)
	{
		if (QueryWorkingSet(process, buffer.data(), bufferSize))
		{
			break;
		}

		if (GetLastError() != ERROR_BAD_LENGTH)
		{
			return 0;
		}

		bufferSize *= 2;
		buffer.resize(bufferSize);
	}

	const auto* wsInfo = reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION*>(buffer.data());

	SYSTEM_INFO si = {};
	GetSystemInfo(&si);
	const SIZE_T pageSize = si.dwPageSize;

	SIZE_T privatePages = 0;
	for (ULONG_PTR i = 0; i < wsInfo->NumberOfEntries; ++i)
	{
		const PSAPI_WORKING_SET_BLOCK block = wsInfo->WorkingSetInfo[i];
		if (block.Shared == 0)
		{
			++privatePages;
		}
	}

	return privatePages * pageSize;
}

void EditorWindow::DrawMemoryPanel()
{
	const HANDLE process = GetCurrentProcess();

	// メモリ使用量の表示
	PROCESS_MEMORY_COUNTERS_EX pmc = {};
	if (GetProcessMemoryInfo(
		process,
		reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
		sizeof(pmc)))
	{
		const double workingSetMB = static_cast<double>(pmc.WorkingSetSize) * BYTES_TO_MB;
		const double privateCommitMB = static_cast<double>(pmc.PrivateUsage) * BYTES_TO_MB;

		const SIZE_T privateWsBytes = GetPrivateWorkingSetBytes(process);
		const double privateWorkingSetMB = static_cast<double>(privateWsBytes) * BYTES_TO_MB;

		ImGui::Text(u8("メモリ(Private Working Set): %.1f MB"), privateWorkingSetMB);
		ImGui::Text(u8("Working Set(共有含む): %.1f MB"), workingSetMB);
		ImGui::Text(u8("Commit Size(PrivateUsage): %.1f MB"), privateCommitMB);
	}
}

#pragma endregion

void EditorWindow::DrawScenePanel(SceneManager& sceneManager)
{
	ImGui::Text(u8("シーンコントロール"));
	ImGui::Separator();

	auto activeScene = sceneManager.GetActiveScene();
	if (!activeScene) return;

	ImGui::InputText(u8("シーン名##SceneName"), m_SceneRegisterName.data(), m_SceneRegisterName.size());
	ImGui::InputText(u8("シーンパス##ScenePath"), m_SceneRegisterPath.data(), m_SceneRegisterPath.size());

	if (ImGui::Button(u8("登録##RegisterScene")))
	{
		std::string name = m_SceneRegisterName.data();
		std::string path = m_SceneRegisterPath.data();

		if (!name.empty())
		{
			auto scene = std::make_unique<RuntimeScene>(path, m_App.GetDevice(), m_App.GetLinePso());
			sceneManager.RegisterScene(name, std::move(scene));
		}
	}

	ImGui::SameLine();
	if (ImGui::Button(u8("ロード##LoadScene")))
	{
		std::string name = m_SceneRegisterName.data();
		if (!name.empty())
		{
			sceneManager.LoadScene(name);
		}
	}

	ImGui::SameLine();
	if (ImGui::Button(u8("保存##SaveScene")))
	{
		SceneSerializer::Save(*activeScene, m_SceneRegisterPath.data());
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

		m_SelectedEntity = entity;
	}

	ImGui::Separator();
	DrawPrefabPanel(*activeScene, activeScene->GetWorld());
}

void EditorWindow::DrawColliderDebug(const ColliderComponent& collider, const TransformComponent& transform)
{
	const float3 pos = transform.position;
	const float4 debugColor = float4(0.0f, 1.0f, 0.0f, 1.0f);
	const float4 debugColorY = float4(1.0f, 0.0f, 0.0f, 1.0f);

	if (collider.shapeType == ColliderComponent::ShapeType::Box)
	{
		// Boxの頂点計算（TransformのスケールをColliderサイズに適用）
		const float3 scaledSize = {
			collider.size.x * transform.scale.x * 0.5f,
			collider.size.y * transform.scale.y * 0.5f,
			collider.size.z * transform.scale.z * 0.5f
		};
		const float3 verticex[8]{
			pos + float3(-scaledSize.x, -scaledSize.y, -scaledSize.z),
			pos + float3(scaledSize.x, -scaledSize.y, -scaledSize.z),
			pos + float3(scaledSize.x, scaledSize.y, -scaledSize.z),
			pos + float3(-scaledSize.x, scaledSize.y, -scaledSize.z),
			pos + float3(-scaledSize.x, -scaledSize.y, scaledSize.z),
			pos + float3(scaledSize.x, -scaledSize.y, scaledSize.z),
			pos + float3(scaledSize.x, scaledSize.y, scaledSize.z),
			pos + float3(-scaledSize.x, scaledSize.y, scaledSize.z)
		};

		// 立方体の12本のエッジを記録
		m_DebugLines.push_back({ verticex[0], verticex[1], debugColor });
		m_DebugLines.push_back({ verticex[1], verticex[2], debugColor });
		m_DebugLines.push_back({ verticex[2], verticex[3], debugColor });
		m_DebugLines.push_back({ verticex[3], verticex[0], debugColor });

		m_DebugLines.push_back({ verticex[4], verticex[5], debugColor });
		m_DebugLines.push_back({ verticex[5], verticex[6], debugColor });
		m_DebugLines.push_back({ verticex[6], verticex[7], debugColor });
		m_DebugLines.push_back({ verticex[7], verticex[4], debugColor });

		m_DebugLines.push_back({ verticex[0], verticex[4], debugColor });
		m_DebugLines.push_back({ verticex[1], verticex[5], debugColor });
		m_DebugLines.push_back({ verticex[2], verticex[6], debugColor });
		m_DebugLines.push_back({ verticex[3], verticex[7], debugColor });
	}
	else if (collider.shapeType == ColliderComponent::ShapeType::Sphere)
	{
		// 球体の半径にスケールを適用
		const float radius = collider.radius * ((transform.scale.x + transform.scale.y + transform.scale.z) / 3.0f);
		const int segments = 16;
		const float pi = 3.14159265f;
		for (int i = 0; i < segments; ++i)
		{
			float angle1 = (2.0f * pi * i) / segments;
			float angle2 = (2.0f * pi * (i + 1)) / segments;

			// XZ平面
			float3 p1 = pos + float3(radius * cosf(angle1), 0.0f, radius * sinf(angle1));
			float3 p2 = pos + float3(radius * cosf(angle2), 0.0f, radius * sinf(angle2));
			m_DebugLines.push_back({ p1, p2, debugColor });

			// YZ平面
			p1 = pos + float3(radius * cosf(angle1), radius * sinf(angle1), 0.0f);
			p2 = pos + float3(radius * cosf(angle2), radius * sinf(angle2), 0.0f);
			m_DebugLines.push_back({ p1, p2, debugColorY });

			// XY平面
			p1 = pos + float3(0.0f, radius * cosf(angle1), radius * sinf(angle1));
			p2 = pos + float3(0.0f, radius * cosf(angle2), radius * sinf(angle2));
			m_DebugLines.push_back({ p1, p2, debugColorY });
		}
	}
	else if (collider.shapeType == ColliderComponent::ShapeType::Capsule)
	{
		// カプセルの半径にスケールを適用
		const float radius = collider.radius * ((transform.scale.x + transform.scale.z) * 0.5f);
		const float height = collider.radius * 4.0f * transform.scale.y;
		const int segments = 16;
		const float pi = 3.14159265359f;

		for (int i = 0; i < segments; ++i)
		{
			float angle1 = (2.0f * pi * i) / segments;
			float angle2 = (2.0f * pi * (i + 1)) / segments;

			// 中央の円
			float3 p1 = pos + float3(radius * cosf(angle1), 0.0f, radius * sinf(angle1));
			float3 p2 = pos + float3(radius * cosf(angle2), 0.0f, radius * sinf(angle2));
			m_DebugLines.push_back({ p1, p2, debugColor });

			// 上下の縦線
			p1 = pos + float3(radius * cosf(angle1), height * 0.5f, radius * sinf(angle1));
			p2 = pos + float3(radius * cosf(angle1), -height * 0.5f, radius * sinf(angle1));
			m_DebugLines.push_back({ p1, p2, debugColor });
		}
	}
}

#include "ModelLoader.hpp"
#include "Systems.hpp"

void EditorWindow::SpawnModelFromFile(World& world, const std::string& modelpath, const float3& pos)
{
	// 1. fbx を読み込む（Application.cpp と同じ
	auto modelData = ModelLoader::LoadFromFile(
		m_App.GetCurrent()->GetDevice(), modelpath, 0.01f);
	if (modelData.mesh == nullptr) return;

	// 2. マテリアル作成
	auto material = MakeShared<Material>();
	material->Init();
	if (!modelData.diffuseTexturePath.empty())
		material->SetTextureFromFile(modelData.diffuseTexturePath);

	// 3. エンティティを作ってコンポーネントを付ける
	Entity e = world.CreateEntity();
	TransformComponent tr{};
	std::string name = "Model_" + std::to_string(e);
	tr.position = pos;
	tr.rotation = float4(0, 0, 0, 1);
	tr.scale = float3(1, 1, 1);
	tr.RebuildWorld();

	world.AddComponent<TransformComponent>(e, tr);
	world.AddComponent<MeshComponent>(e, MeshComponent{ modelData.mesh });
	world.AddComponent<MaterialComponent>(e, MaterialComponent{ material });
	world.AddComponent<NameComponent>(e, NameComponent{ name });

	NameSytem::SetName(world, e,name);

	m_SelectedEntity = e; // 置いたものを選択状態に
}

