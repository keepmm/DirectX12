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
#include "Util.hpp"
#include "BuildSystem.hpp"
#include <ShObjIdl.h>

#pragma comment(lib, "psapi.lib")

// ワイド文字列 → UTF-8(フォルダ選択ダイアログの結果用)
static std::string WideToUTF8(const wchar_t* w)
{
	if (!w) return {};
	int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 1) return {};
	std::string result(len - 1, '\0');   // len は終端NUL込み
	WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), len, nullptr, nullptr);
	return result;
}

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

EditorWindow::EditorWindow(DirectXApp& app, SceneManager& sceneManager)
	: m_App(app)
{
	// ファイルパスの初期値を設定
	if (Scene* s = sceneManager.GetActiveScene())
	{
		std::snprintf(m_SceneRegisterName.data(), m_SceneRegisterName.size(),
			"%s", s->GetSceneName().c_str());
		std::snprintf(m_SceneRegisterPath.data(), m_SceneRegisterPath.size(),
			"%s", s->GetSceneName().c_str());   // Sceneがパスを持っていなければ追加
	}

	// ゲーム画面用のレンダーテクスチャ初期化
	m_GameRenderTexture = std::make_unique<RenderTexture>();
	if (FAILED(m_GameRenderTexture->Init(1280, 720)))
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
	if (FAILED(m_EditorRenderTexture->Init(1280, 720)))
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
	BuildSystem::Update();
	// ビルド中は右下に進捗オーバーレイを表示
	if (BuildSystem::IsBuilding() || (BuildSystem::GetProgress() >= 1.0f && m_BuildOverlayTimer > 0.0f))
	{
		if (BuildSystem::IsBuilding()) m_BuildOverlayTimer = 2.0f;      // 完了後2秒だけ残す
		else                           m_BuildOverlayTimer -= ImGui::GetIO().DeltaTime;

		ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(
			ImVec2(vp->WorkPos.x + vp->WorkSize.x - 10.0f,
				vp->WorkPos.y + vp->WorkSize.y - 10.0f),
			ImGuiCond_Always, ImVec2(1.0f, 1.0f));   // 右下基準
		ImGui::SetNextWindowBgAlpha(0.85f);
		ImGui::Begin(u8("ビルド進捗"), nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);

		float p = BuildSystem::GetProgress();
		// MSBuild 区間(進捗が動かない)はバーを流れるアニメーションにする
		if (BuildSystem::IsBuilding() && p <= 0.05f)
			ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(260, 0), u8("MSBuild..."));
		else
			ImGui::ProgressBar(p, ImVec2(260, 0));

		ImGui::TextUnformatted(BuildSystem::GetStage().c_str());
		ImGui::End();
	}
	ImGuizmo::BeginFrame();

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
					SceneSerializer::Save(*activeScene,
					SceneManager::ScenePathFromName(activeScene->GetSceneName()));
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
			ImGui::Checkbox(u8("コンソールを表示"), &m_ShowConsole);
			ImGui::Checkbox(u8("スタイル設定を表示"), &m_ShowStyleSetting);
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu(u8("ビルド")))
		{
			ImGui::InputText(u8("出力先"), m_BuildOutputDir.data(), m_BuildOutputDir.size());
			ImGui::SameLine();
			if (ImGui::Button(u8("参照...###BuildOutputDir")))
			{
				ComPtr<IFileOpenDialog> dialog;
				if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
					CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
				{
					DWORD opts = 0;
					dialog->GetOptions(&opts);
					dialog->SetOptions(opts | FOS_PICKFOLDERS);
					if (SUCCEEDED(dialog->Show(nullptr)))
					{
						ComPtr<IShellItem> item;
						PWSTR pathW = nullptr;
						if (SUCCEEDED(dialog->GetResult(&item)) &&
							SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)))
						{
							std::string utf8 = WideToUTF8(pathW);
							std::snprintf(m_BuildOutputDir.data(), m_BuildOutputDir.size(), "%s", utf8.c_str());
							CoTaskMemFree(pathW);
						}
					}
				}
			}
			static int configIndex = 0;
			ImGui::Combo(u8("構成"), &configIndex, "Release\0Debug\0");
			ImGui::InputText(u8("ゲーム名"), m_BuildGameName.data(), m_BuildGameName.size());
			ImGui::InputText(u8("開始シーン"), m_BuildStartScene.data(), m_BuildStartScene.size());
			ImGui::SameLine();
			if(ImGui::Button(u8("参照...###BuildStartScene")))
			{
				ComPtr<IFileOpenDialog> dialog;
				if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
					CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
				{
					DWORD opts = 0;
					dialog->GetOptions(&opts);
					dialog->SetOptions(opts | FOS_FILEMUSTEXIST);
					if (SUCCEEDED(dialog->Show(nullptr)))
					{
						ComPtr<IShellItem> item;
						PWSTR pathW = nullptr;
						if (SUCCEEDED(dialog->GetResult(&item)) &&
							SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)))
						{
							std::string utf8 = WideToUTF8(pathW);
							std::snprintf(m_BuildStartScene.data(), m_BuildStartScene.size(), "%s", utf8.c_str());
							CoTaskMemFree(pathW);
						}
					}
				}
			}

			ImGui::BeginDisabled(BuildSystem::IsBuilding());
			if (ImGui::MenuItem(u8("ゲームをビルド")))
			{
				BuildSetting s;
				s.outputDir = m_BuildOutputDir.data();
				s.gameName = m_BuildGameName.data();
				s.startScene = m_BuildStartScene.data();
				s.configuration = (configIndex == 0) ? "Release" : "Debug";
				BuildSystem::Build(s);
			}
			ImGui::EndDisabled();

			if (BuildSystem::IsBuilding())
				ImGui::TextUnformatted(u8("ビルド中..."));
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	// ドッキングスペースのセットアップ
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	float h = ImGui::GetFrameHeight();
	if (ImGui::BeginViewportSideBar("##PlayToolbar", viewport, ImGuiDir_Up, h,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			DrawPlayControl(activeScene);
			ImGui::EndMenuBar();
		}
	}
	ImGui::End();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
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
		ImGuiID dockLeftID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Left, 0.12f, nullptr, &dockMainID);
		ImGuiID dockRightID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Right, 0.15f, nullptr, &dockMainID);
		ImGuiID dockBottomID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Down, 0.20f, nullptr, &dockMainID);

		ImGui::DockBuilderDockWindow(u8("アウトライナー"), dockLeftID);
		ImGui::DockBuilderDockWindow(u8("ゲーム画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("エディタ画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("プロパティパネル"), dockRightID);
		ImGui::DockBuilderDockWindow(u8("詳細パネル"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("コンソール"), dockBottomID);

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

	INPUT->SetViewportHovered(false);
	// ---- ビューポートパネル ---- //
	if (ImGui::Begin(u8("ゲーム画面")) && m_ShowViewport)
	{
		ImVec2 availableSize = ImGui::GetContentRegionAvail();
		if (ImGui::IsWindowHovered())
			INPUT->SetViewportHovered(true);

		// -------------------------------------------------------------------- //
		//	ビューポートのサイズが変更された場合、レンダーテクスチャもリサイズ  //
		// -------------------------------------------------------------------- //
		const UINT newWidth = static_cast<UINT>(availableSize.x);
		const UINT newHeight = static_cast<UINT>(availableSize.y);
		if (m_GameRenderTexture && newWidth > 0 && newHeight > 0 &&
			(newWidth != m_GameRenderTexture->GetWidth() || newHeight != m_GameRenderTexture->GetHeight()))
		{
			m_App.WaitForGPUIdle();
			m_GameRenderTexture->Init(newWidth, newHeight);
		}

		m_ViewportPos = ImGui::GetCursorScreenPos();
		m_ViewportSize = availableSize;

		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_GameRenderTexture && m_GameTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_GameRenderTexture->GetSRV().ptr),
				availableSize, ImVec2(0, 0), ImVec2(1, 1));

			// ---- シーンフェイド処理 ---- //
			const float fade = sceneManager.GetFadeAlpha();
			if (fade > 0.0f)
			{
				const ImVec2 p0 = ImGui::GetItemRectMin();
				const ImVec2 p1 = ImGui::GetItemRectMax();
				const ImU32 col = ImGui::GetColorU32(ImVec4(0, 0, 0, fade));
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
			}

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

					SpawnModelFromFile(activeScene->GetWorld(), modelpath, fragPosition,activeScene);
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
		if (ImGui::IsWindowHovered())
			INPUT->SetViewportHovered(true);
		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_EditorRenderTexture && m_EditorTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_EditorRenderTexture->GetSRV().ptr),
				availableSize, ImVec2(0, 0), ImVec2(1, 1));

			// ---- Gizmoの描画 ---- //
			if (activeScene && m_SelectedEntity != INVALID_ENTITY)
			{
				World& gw = activeScene->GetWorld();
				if (gw.IsEntityAlive(m_SelectedEntity) &&
					gw.HasComponent<TransformComponent>(m_SelectedEntity))
				{
					// エディタ画面の描画に使うカメラ（Main→Secondaryの順）
					const CameraComponent* cam = nullptr;
					const CameraComponent* fallback = nullptr;
					gw.Each<CameraComponent>([&](Entity, CameraComponent& c) {
						if (c.cameraType == CameraComponent::CameraType::Secondary) cam = &c;
						else                                                        fallback = &c;
						});
					if (cam == nullptr) cam = fallback;   // Secondaryが無ければMainで代用

					if (cam)
					{
						// ギズモの描画先と領域を、直前のImageに合わせる
						ImGuizmo::SetOrthographic(false);
						ImGuizmo::SetDrawlist();
						const ImVec2 imgPos = ImGui::GetItemRectMin();
						const ImVec2 imgSize = ImGui::GetItemRectSize();
						ImGuizmo::SetRect(imgPos.x, imgPos.y, imgSize.x, imgSize.y);

						auto& tr = gw.GetComponent<TransformComponent>(m_SelectedEntity);
						float4x4 world = tr.world;

						if (ImGuizmo::Manipulate(
							&cam->view._11, &cam->proj._11,
							static_cast<ImGuizmo::OPERATION>(m_GizmoOperation),
							ImGuizmo::LOCAL,
							&world._11))
						{
							DirectX::XMMATRIX newLocal = DirectX::XMLoadFloat4x4(&world);

							// 親がいる場合は親空間へ戻す
							if (tr.parent != INVALID_ENTITY && tr.parent != m_SelectedEntity &&
								gw.HasComponent<TransformComponent>(tr.parent))
							{
								const auto& pt = gw.GetComponent<TransformComponent>(tr.parent);
								DirectX::XMVECTOR det;
								DirectX::XMMATRIX inv = DirectX::XMMatrixInverse(&det, DirectX::XMLoadFloat4x4(&pt.world));
								if (!DirectX::XMVectorGetX(DirectX::XMVectorEqual(det, DirectX::XMVectorZero())))
								{
									newLocal = newLocal * inv;
								}
							}

							vector s, q, t;
							if (DirectX::XMMatrixDecompose(&s, &q, &t, newLocal))
							{
								DirectX::XMStoreFloat3(&tr.position,t);
								DirectX::XMStoreFloat4(&tr.rotation,q);
								DirectX::XMStoreFloat3(&tr.scale,s);
								tr.SyncEulerFromQuaternion();
								tr.RebuildWorld();
							}

							//// 行列を位置、回転、サイズに分解
							//float t[3], r[3], s[3];
							//ImGuizmo::DecomposeMatrixToComponents(&world._11, t, r, s);

							//tr.position = float3(t[0], t[1], t[2]);
							//const auto q = DirectX::XMQuaternionRotationRollPitchYaw(
							//	DirectX::XMConvertToRadians(r[0]),
							//	DirectX::XMConvertToRadians(r[1]),
							//	DirectX::XMConvertToRadians(r[2]));
							//DirectX::XMStoreFloat4(&tr.rotation, q);
							//tr.scale = float3(s[0], s[1], s[2]);
							//tr.ApplyEuler(); // Euler角を更新
							//tr.RebuildWorld(); // ワールド行列を更新
						}
					}
				}
			}

			if (ImGui::IsWindowFocused())
			{
				if (ImGui::IsKeyPressed(ImGuiKey_W)) m_GizmoOperation = ImGuizmo::TRANSLATE;
				if (ImGui::IsKeyPressed(ImGuiKey_E)) m_GizmoOperation = ImGuizmo::ROTATE;
				if (ImGui::IsKeyPressed(ImGuiKey_R)) m_GizmoOperation = ImGuizmo::SCALE;
			}
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

	if(ImGui::Begin(u8("コンソール")) && m_ShowConsole)
	{
		DrawConsole();
	}
	ImGui::End();

	// ---- 詳細パネル ---- //
	if (ImGui::Begin(u8("詳細パネル")) && m_ShowDetails)
	{
		if (ImGui::BeginTabBar(u8("詳細パネルタブ")))
		{
			if (ImGui::BeginTabItem(u8("アセット")))
			{
				DrawAssetPanel(sceneManager);
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

	DrawStyleSetting();
}

void EditorWindow::ReleaseRenderTextures()
{
	if (m_GameRenderTexture) { m_GameRenderTexture->Release();   m_GameRenderTexture.reset(); }
	if (m_EditorRenderTexture) { m_EditorRenderTexture->Release(); m_EditorRenderTexture.reset(); }
}

void EditorWindow::DrawSceneInfo(Scene& scene)
{
	ImGui::Text(u8("シーン: %s"), scene.GetSceneName().c_str());
	ImGui::Separator();
	ImGui::Checkbox("Deferred Rendering", &RenderSettings::Get().deferred);
	ImGui::Separator();
}

void EditorWindow::DrawEntityList(World& world)
{
	ImGui::Text(u8("エンティティ一覧"));
	ImGui::InputText(u8("##FilterEntity"), m_EntityFilyer.data(), m_EntityFilyer.size());

	if (ImGui::BeginChild("EntityList", ImVec2(0.0f, 0.0f), true))
	{
		//   TransformもRectTransformも無いエンティティも拾う
		for (Entity e : world.GetEntities())
		{
			if (GetParent(world, e) == INVALID_ENTITY)   // ルートだけ
				DrawEntityNode(world, e);
		}

		// 余白へのドロップ＝ルートに戻す
		ImGui::Dummy(ImGui::GetContentRegionAvail());
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
			{
				Entity child = *(const Entity*)p->Data;
				SetParent(world, child, INVALID_ENTITY);
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextWindow())
		{
			if (ImGui::BeginMenu(u8("作成")))
			{
				if (ImGui::MenuItem(u8("空のエンティティ")))
				{
					static int entityCount = 1;
					Entity e = world.CreateEntity();
					world.AddComponent<NameComponent>(e, NameComponent{ "Entity " + std::to_string(entityCount++) });
					world.AddComponent<TransformComponent>(e, TransformComponent{});
					m_SelectedEntity = e;
				}
				if (ImGui::BeginMenu("UI"))
				{
					if (ImGui::MenuItem("Image")) m_SelectedEntity = CreateImage(world);
					if (ImGui::MenuItem("Text"))  m_SelectedEntity = CreateText(world);
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}
			if (ImGui::MenuItem(u8("エンティティを削除")) && m_SelectedEntity != INVALID_ENTITY)
			{
				APP->WaitForGPUIdle();
				world.DestroyEntity(m_SelectedEntity);
				m_SelectedEntity = INVALID_ENTITY;
			}
			ImGui::EndPopup();
		}
	}
	ImGui::EndChild(); 
}

void EditorWindow::DrawEntityNode(World& world, Entity entity)
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
	if (m_SelectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;
	if (!hasChildren)               flags |= ImGuiTreeNodeFlags_Leaf;  // 子無しは?を出さない

	ImGui::PushID((int)entity);
	bool open = ImGui::TreeNodeEx(label.c_str(), flags);

	// クリックで選択
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		m_SelectedEntity = entity;

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
				SetParentKeepWorld(world, child, entity);
		}
		ImGui::EndDragDropTarget();
	}

	// 開いていれば子を再帰描画
	if (open)
	{
		world.Each<TransformComponent>([&](Entity e, TransformComponent& t) {
			if (t.parent == entity)
				DrawEntityNode(world, e);
			});
		ImGui::TreePop();
	}
	ImGui::PopID();
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

void EditorWindow::DrawConsole()
{
	if (ImGui::Button(u8("クリア"))) { /* 後述: Logger側にClear追加 */ }
	ImGui::SameLine();
	static bool autoScroll = true;
	ImGui::Checkbox(u8("自動スクロール"), &autoScroll);

	ImGui::Separator();
	ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false,
		ImGuiWindowFlags_HorizontalScrollbar);

	for (const auto& line : LOG->GetRecentLogs())
	{
		std::string utf8 = IMGUI::ToUTF8(line);

		ImVec4 color(1, 1, 1, 1);
		if (line.find("[ERROR]") != std::string::npos) color = ImVec4(1.0f, 0.3f, 0.3f, 1);
		else if (line.find("[WARNING]") != std::string::npos) color = ImVec4(1.0f, 0.85f, 0.3f, 1);
		else if (line.find("[DEBUG]") != std::string::npos) color = ImVec4(0.5f, 0.7f, 1.0f, 1);

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s", utf8.c_str());
		ImGui::PopStyleColor();
	}

	if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
}

void EditorWindow::DrawPlayControl(Scene* activeScene)
{
	const EngineMode mode = PLAY.GetCurrentMode();
	const bool isEditor = (mode == EngineMode::EDITOR);

	if(ImGui::Button(isEditor ? u8("Play") : u8("Stop")))
	{
		if (isEditor)
		{
			APP->WaitForGPUIdle();
			if(activeScene)
				m_PlaySnap = SceneSerializer::SaveToString(*activeScene);
			PLAY.SetMode(EngineMode::Play);
		}
		else
		{
			PLAY.SetMode(EngineMode::EDITOR);
			APP->WaitForGPUIdle();
			if(activeScene && !m_PlaySnap.empty())
			{
				SceneSerializer::LoadFromString(*activeScene, m_PlaySnap);
			}
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(isEditor);
	if(ImGui::Button(mode == EngineMode::PAUSE ? u8("Resume") : u8("Pause")))
	{
		PLAY.SetMode(mode == EngineMode::PAUSE ? EngineMode::Play : EngineMode::PAUSE);
	}
	ImGui::EndDisabled();
}

#pragma endregion

void EditorWindow::DrawScenePanel(SceneManager& sceneManager)
{
	ImGui::Text(u8("シーンコントロール"));
	ImGui::Separator();

	auto activeScene = sceneManager.GetActiveScene();
	if (!activeScene) return;

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

		m_SelectedEntity = entity;
	}

	ImGui::Separator();
	DrawPrefabPanel(*activeScene, activeScene->GetWorld());
}

#pragma region Canvas
// todo 親子化
// todo Canvasの描画順序を考慮する
// todo image, textの名前被りの解消

Entity EditorWindow::EnsureCanvas(World& world)
{
	// 宣言
	Entity canvas = INVALID_ENTITY;

	// 既に宣言されるか確認
	world.Each<CanvasComponent>([&](Entity e, CanvasComponent&)
		{
			// すでにCanvasが存在する場合はそれを返す
			if(canvas == INVALID_ENTITY)
				canvas = e;
		});

	// 見つからない場合は新規作成
	if (canvas == INVALID_ENTITY)
	{
		canvas = world.CreateEntity();
		world.AddComponent<NameComponent>(canvas, NameComponent{ "Canvas" });
		world.AddComponent<RectTransformComponent>(canvas, RectTransformComponent{});
		world.AddComponent<CanvasComponent>(canvas, CanvasComponent{});
	}

	// 値を返す
	return canvas;
}

Entity EditorWindow::CreateImage(World& world)
{
	// canvasがあるか確認
	// ない場合は作成
	EnsureCanvas(world);
	Entity e = world.CreateEntity();
	static int num = 1;
	std::string name = "Image_" + std::to_string(num++);
	world.AddComponent<NameComponent>(e, NameComponent{ name });
	auto& rt = world.AddComponent<RectTransformComponent>(e, RectTransformComponent{});
	rt.SizeDelta = { 100.0f,100.0f };
	world.AddComponent<UIImageComponent>(e, UIImageComponent{});
	return e;
}

Entity EditorWindow::CreateText(World& world)
{
	// canvasがあるか確認
	EnsureCanvas(world);
	Entity e = world.CreateEntity();

	static int num = 1;
	std::string name = "Text_" + std::to_string(num++);
	world.AddComponent<NameComponent>(e, NameComponent{ name });
	auto& rt = world.AddComponent<RectTransformComponent>(e, RectTransformComponent{});
	rt.SizeDelta = { 100.0f,50.0f };
	world.AddComponent<UITextComponent>(e, UITextComponent{});
	return e;
}

#pragma endregion

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

#include "Theme.hpp"
void EditorWindow::DrawStyleSetting()
{
	//---- スタイル設定 ---- //
	if (!m_ShowStyleSetting) return;

	// 独立ウィンドウ
	if (ImGui::Begin(u8("スタイル設定"), &m_ShowStyleSetting))
	{
		// ---- テーマ ---- //
		static int themeIdx = 0;
		static ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
		const char* themes[] = { "Dark", "Light", "Classic" };
		if (ImGui::Combo(u8("テーマ"), &themeIdx, themes, IM_ARRAYSIZE(themes)))
			ApplyTheme((uiTheme)themeIdx, accent);
		if (ImGui::ColorEdit3(u8("アクセント色"), &accent.x))
			ApplyTheme((uiTheme)themeIdx, accent);

		ImGui::Separator();

		// ---- フォント切替 ----
		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::BeginCombo(u8("フォント"),
			io.FontDefault ? "current" : "default"))
		{
			for (int i = 0; i < io.Fonts->Fonts.Size; ++i)
			{
				ImFont* f = io.Fonts->Fonts[i];
				ImGui::PushID(i);
				bool sel = (io.FontDefault == f);
				// フォント名は登録時のデバッグ名が入る
				if (ImGui::Selectable(u8(f->GetDebugName()), sel))
					io.FontDefault = f;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}
	ImGui::End();
}


#include "ModelLoader.hpp"
#include "Systems.hpp"

void EditorWindow::SpawnModelFromFile(World& world, const std::string& modelpath, const float3& pos, Scene* scene)
{
	Entity e = world.CreateEntity();
	// 先にTransform/名前だけ付けておく（メッシュは後から差し込む）
	TransformComponent tr{}; tr.position = pos; tr.RebuildWorld();
	world.AddComponent<TransformComponent>(e, tr);
	world.AddComponent<NameComponent>(e, NameComponent{ "Model_" + std::to_string(e) });

	ModelLoader::PopulateModelEntity(world, e, modelpath, scene);
	m_SelectedEntity = e;
}

void EditorWindow::CreateScriptFile(const std::string& die, const std::string& name)
{
	namespace fs = std::filesystem;
	fs::path hpp = fs::absolute(fs::path(die)) / (name + ".hpp");
	fs::path cpp = fs::absolute(fs::path(die)) / (name + ".cpp");
	if (fs::exists(hpp))
	{
		LOG->LogWarning("スクリプトファイルが既に存在します: " + hpp.string());
	}
	if(fs::exists(cpp))
	{
		LOG->LogWarning("スクリプトファイルが既に存在します: " + cpp.string());
	}

	std::ofstream out(hpp);
	out <<
		"#pragma once\n"
		"#include \"MonoBehavior.hpp\"\n"
		"#include \"Logger.hpp\"\n"
		"#include \"Components.hpp\"\n\n"
		"class " << name << " : public MonoBehavior\n"
		"{\n"
		"public:\n"
		"    void OnStart() override {}\n"
		"    void OnUpdate(float dt) override\n"
		"    {\n"
		"        // auto& tr = transform();\n"
		"        LOG->LogInfo(\"[" << name << "] Update\");\n"
		"    }\n"
		"};\n";

	std::ofstream outcpp(cpp);
	outcpp <<
		"#include \"" << name << ".hpp\"\n"
		"#include \"RegisterScript.hpp\"\n\n"
		"REGISTER_SCRIPT(" << name << ");\n";

	// vsproj にも追加
	auto toProjRel = [](const fs::path& p)
		{
			std::string s = p.lexically_normal().string();
			std::replace(s.begin(), s.end(), '/', '\\');
			return s;
		};
	out.close();
	outcpp.close();

	// プロジェクトに追加
	AddToProject(toProjRel(cpp), toProjRel(hpp));

	LOG->LogInfo("スクリプト生成: " + hpp.string());
}

void EditorWindow::OpenInEditor(const std::string& path)
{
	namespace fs = std::filesystem;

	// exeからソリューションを逆算して .slnを探索
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	fs::path slnDir = fs::path(exePath).parent_path().parent_path().parent_path();
	fs::path sln = slnDir / "DirectX12__test.sln";

	std::wstring wpath = std::filesystem::path(path).wstring();
	std::wstring args = L"/edit \"" + wpath + L"\"";
	ShellExecuteW(NULL, L"open", L"devenv.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void EditorWindow::AddToProject(const std::string cppPath, const std::string hppPath)
{
	namespace fs = std::filesystem;

	// exe の場所から solutionDir を逆算（sln\x64\Debug\exe -> sln）
	char exePath[MAX_PATH];
	GetModuleFileNameA(nullptr, exePath, MAX_PATH);
	fs::path slnDir = fs::path(exePath).parent_path().parent_path().parent_path();
	fs::path scriptProj = slnDir / "Scripts" / "Scripts.vcxproj";

	if (!fs::exists(scriptProj))
	{
		LOG->LogWarning("プロジェクトが見つかりません: " + scriptProj.string());
		return;
	}

	// vcxproj 読み込み
	std::ifstream in(scriptProj);
	std::string xml((std::istreambuf_iterator<char>(in)), {});
	in.close();

	// 絶対パス（Windows区切り）
	auto toWin = [](const std::string& p)
		{
			return fs::absolute(p).make_preferred().string();
		};
	const std::string cppAbs = toWin(cppPath);
	const std::string hppAbs = toWin(hppPath);

	// 既に登録済みなら何もしない（二重登録防止）
	if (xml.find(cppAbs) != std::string::npos) return;

	// 挿入する行
	const std::string includeEntry =
		"    <ClInclude Include=\"" + hppAbs + "\" />\r\n";
	const std::string compileEntry =
		"    <ClCompile Include=\"" + cppAbs + "\">\r\n"
		"      <PrecompiledHeader>NotUsing</PrecompiledHeader>\r\n"
		"    </ClCompile>\r\n";

	// 行頭位置を求めるヘルパ
	auto lineHead = [&](size_t pos)
		{
			size_t nl = xml.rfind('\n', pos);
			return (nl == std::string::npos) ? size_t(0) : nl + 1;
		};

	// <ClInclude Include="framework.h" /> の前に hpp を挿入
	if (size_t p = xml.find("<ClInclude Include=\"framework.h\" />"); p != std::string::npos)
		xml.insert(lineHead(p), includeEntry);

	// <ClCompile Include="cr_main.cpp" /> の前に cpp を挿入
	if (size_t p = xml.find("<ClCompile Include=\"cr_main.cpp\" />"); p != std::string::npos)
		xml.insert(lineHead(p), compileEntry);

	// 書き戻し（BOMなし・改行そのまま）
	std::ofstream out(scriptProj, std::ios::binary | std::ios::trunc);
	out << xml;
	out.close();

	LOG->LogInfo("Scripts.vcxproj に追加: " + fs::path(cppPath).filename().string());
}

void EditorWindow::CreateFolder(const std::string& dir)
{
	namespace fs = std::filesystem;

	// "New Folder", "New Folder 1", ... と重複回避
	fs::path target = fs::path(dir) / u8("New Folder");
	int n = 1;
	while (fs::exists(target))
		target = fs::path(dir) / (std::string(u8("New Folder ")) + std::to_string(n++));

	std::error_code ec;
	fs::create_directory(target, ec);
	if (ec) LOG->LogWarning("フォルダ作成失敗: " + ec.message());
	else    LOG->LogInfo("フォルダ作成: " + target.string());
}