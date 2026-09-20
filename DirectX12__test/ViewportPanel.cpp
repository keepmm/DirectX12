/*!*************************************************************
 * \file   ViewportPanel.cpp
 * \brief  ゲーム画面 / エディタ画面のビューポート
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "ViewportPanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "DirectX.hpp"
#include "Input.hpp"
#include "Components.hpp"
#include "SceneManager.hpp"
#include "EntityFactory.hpp"
#include "UndoHistory.hpp"
#include "Terrain.hpp"
#include "AssetDatabase.hpp"
#include <algorithm>

#undef max
#undef min

void ViewportPanel::Init()
{
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

void ViewportPanel::ReleaseRenderTextures()
{
	if (m_GameRenderTexture) { m_GameRenderTexture->Release();   m_GameRenderTexture.reset(); }
	if (m_EditorRenderTexture) { m_EditorRenderTexture->Release(); m_EditorRenderTexture.reset(); }
}

/// @brief レンダーテクスチャが無いときの塗りつぶし
static void DrawFallback(const EditorContext& ctx, const char* message)
{
	LOG->LogError(message);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 backgroundColor = ImGui::GetColorU32(ImGuiCol_WindowBg);
	drawList->AddRectFilled(
		ctx.viewportPos,
		ImVec2(ctx.viewportPos.x + ctx.viewportSize.x, ctx.viewportPos.y + ctx.viewportSize.y),
		backgroundColor);
}

void ViewportPanel::DrawGameView(EditorContext& ctx)
{
	// 閉じている間は描画も省く(Application が IsGameViewVisible を見てシーン描画を止める)
	if (!showGameView)
	{
		m_GameViewVisible = false;
		return;
	}

	const bool open = ImGui::Begin(u8("ゲーム画面"), &showGameView);
	m_GameViewVisible = open && showGameView;

	if (m_GameViewVisible)
	{
		// ---- 縦横比の選択 ---- //
		static const char* kAspectNames[] = { "Free", "16:9", "16:10", "4:3", "1:1", "Original Solution"};
		ImGui::SetNextItemWidth(200.0f);
		ImGui::Combo("##GameAspect", &m_AspectMode, kAspectNames, IM_ARRAYSIZE(kAspectNames));

		if (m_AspectMode == 5)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(70.0f);
			ImGui::InputInt("##GameW", &m_CustomWidth, 0);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(70.0f);
			ImGui::InputInt("##GameH", &m_CustomHeight, 0);
			m_CustomWidth = (std::max)(16, m_CustomWidth);
			m_CustomHeight = (std::max)(16, m_CustomHeight);
		}

		const ImVec2 availableSize = ImGui::GetContentRegionAvail();
		if (ImGui::IsWindowHovered())
			INPUT->SetViewportHovered(true);

		// 選んだ比率に合わせて、枠に収まる最大の大きさを出す(余りは黒帯)
		float aspect = availableSize.y > 0.0f ? availableSize.x / availableSize.y : 16.0f / 9.0f;
		switch (m_AspectMode)
		{
		case 1: aspect = 16.0f / 9.0f;  break;
		case 2: aspect = 16.0f / 10.0f; break;
		case 3: aspect = 4.0f / 3.0f;   break;
		case 4: aspect = 1.0f;          break;
		case 5: aspect = (float)m_CustomWidth / (float)m_CustomHeight; break;
		default: break;	// 自由: 枠そのまま
		}
		RenderSettings::Get().gameAspect = aspect;

		ImVec2 drawSize = availableSize;
		if (m_AspectMode != 0)
		{
			drawSize.x = (std::min)(availableSize.x, availableSize.y * aspect);
			drawSize.y = drawSize.x / aspect;
		}

		// 中央に寄せる
		const ImVec2 origin = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(
			origin.x + (availableSize.x - drawSize.x) * 0.5f,
			origin.y + (availableSize.y - drawSize.y) * 0.5f));

		// -------------------------------------------------------------------- //
		//	ビューポートのサイズが変更された場合、レンダーテクスチャもリサイズ  //
		// -------------------------------------------------------------------- //
		const UINT newWidth = static_cast<UINT>(drawSize.x);
		const UINT newHeight = static_cast<UINT>(drawSize.y);
		if (m_GameRenderTexture && newWidth > 0 && newHeight > 0 &&
			(newWidth != m_GameRenderTexture->GetWidth() || newHeight != m_GameRenderTexture->GetHeight()))
		{
			ctx.app.WaitForGPUIdle();
			m_GameRenderTexture->Init(newWidth, newHeight);
		}

		ctx.viewportPos = ImGui::GetCursorScreenPos();
		ctx.viewportSize = drawSize;

		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_GameRenderTexture && m_GameTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_GameRenderTexture->GetSRV().ptr),
				drawSize, ImVec2(0, 0), ImVec2(1, 1));

			// ---- シーンフェイド処理 ---- //
			const float fade = ctx.sceneManager.GetFadeAlpha();
			if (fade > 0.0f)
			{
				const ImVec2 p0 = ImGui::GetItemRectMin();
				const ImVec2 p1 = ImGui::GetItemRectMax();
				const ImU32 col = ImGui::GetColorU32(ImVec4(0, 0, 0, fade));
				ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
			}

			if (ImGui::BeginDragDropTarget())
			{
				const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_MODEL");
				if (payload != nullptr && ctx.activeScene != nullptr)
				{
					// 運ばれてきたファイルパスを取り出す
					const std::string modelpath(static_cast<const char*>(payload->Data));

					// とりあえず原点に
					const float3 fragPosition = float3(0.0f, 0.0f, 0.0f);

					const size_t before = ctx.activeScene->GetWorld().GetEntities().size();
					ctx.selectedEntity = EntityFactory::SpawnModelFromFile(
						ctx.activeScene->GetWorld(), modelpath, fragPosition, ctx.activeScene);
					if (ctx.history) ctx.history->RecordCreated(*ctx.activeScene, before);
				}
				ImGui::EndDragDropTarget();
			}
		}
		else
		{
			DrawFallback(ctx, "レンダーテクスチャが未初期化です");
		}
	}
	ImGui::End();
}

void ViewportPanel::DrawEditorView(EditorContext& ctx)
{
	if (!showEditorView)
	{
		m_EditorViewVisible = false;
		return;
	}

	const bool open = ImGui::Begin(u8("エディタ画面"), &showEditorView);
	m_EditorViewVisible = open && showEditorView;

	if (m_EditorViewVisible)
	{
		const ImVec2 availableSize = ImGui::GetContentRegionAvail();
		if (ImGui::IsWindowHovered())
			INPUT->SetViewportHovered(true);

		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_EditorRenderTexture && m_EditorTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_EditorRenderTexture->GetSRV().ptr),
				availableSize, ImVec2(0, 0), ImVec2(1, 1));

			// ---- Gizmoの描画 ---- //
			if (ctx.activeScene && ctx.selectedEntity != INVALID_ENTITY)
			{
				World& gw = ctx.activeScene->GetWorld();
				if (gw.IsEntityAlive(ctx.selectedEntity) &&
					gw.HasComponent<TransformComponent>(ctx.selectedEntity))
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

						auto& tr = gw.GetComponent<TransformComponent>(ctx.selectedEntity);
						float4x4 world = tr.world;

						// 地形編集中はギズモを止める(両方動くと操作が喧嘩する)
						const bool brushUsed = DrawTerrainBrush(ctx, *cam, imgPos, imgSize);

						if (!brushUsed && ImGuizmo::Manipulate(
							&cam->view._11, &cam->proj._11,
							static_cast<ImGuizmo::OPERATION>(m_GizmoOperation),
							ImGuizmo::LOCAL,
							&world._11))
						{
							DirectX::XMMATRIX newLocal = DirectX::XMLoadFloat4x4(&world);

							// 親がいる場合は親空間へ戻す
							if (tr.parent != INVALID_ENTITY && tr.parent != ctx.selectedEntity &&
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
								DirectX::XMStoreFloat3(&tr.position, t);
								DirectX::XMStoreFloat4(&tr.rotation, q);
								DirectX::XMStoreFloat3(&tr.scale, s);
								tr.SyncEulerFromQuaternion();
								tr.RebuildWorld();
							}
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
			DrawFallback(ctx, "エディタ用レンダーテクスチャが未初期化です");
		}
	}
	ImGui::End();
}

bool ViewportPanel::DrawTerrainBrush(
	EditorContext& ctx, const CameraComponent& cam, const ImVec2& imgPos, const ImVec2& imgSize)
{
	using namespace DirectX;
	if (!ctx.activeScene || ctx.selectedEntity == INVALID_ENTITY) return false;

	World& world = ctx.activeScene->GetWorld();
	if (!world.HasComponent<TerrainComponent>(ctx.selectedEntity)) return false;
	if (!world.HasComponent<TransformComponent>(ctx.selectedEntity)) return false;

	auto& terrain = world.GetComponent<TerrainComponent>(ctx.selectedEntity);
	const auto& tr = world.GetComponent<TransformComponent>(ctx.selectedEntity);

	// ---- 操作パネル(画面の左上に重ねる) ---- //
	ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8.0f, imgPos.y + 8.0f));
	ImGui::BeginGroup();
	ImGui::Checkbox(u8("地形編集"), &m_TerrainEditing);
	if (m_TerrainEditing)
	{
		static const std::string kRaise = IMGUI::ToUTF8("盛り上げ");
		static const std::string kLower = IMGUI::ToUTF8("削る");
		static const std::string kSmooth = IMGUI::ToUTF8("ならす");
		static const std::string kFlatten = IMGUI::ToUTF8("平坦化");

		const char* kModes[] = { kRaise.c_str(), kLower.c_str(), kSmooth.c_str(), kFlatten.c_str() };
		int mode = static_cast<int>(m_TerrainBrush.mode);
		ImGui::SetNextItemWidth(110.0f);
		if (ImGui::Combo("##TerrainMode", &mode, kModes, IM_ARRAYSIZE(kModes)))
			m_TerrainBrush.mode = static_cast<Terrain::BrushMode>(mode);

		ImGui::SetNextItemWidth(110.0f);
		ImGui::SliderFloat(u8("半径"), &m_TerrainBrush.radius, 0.5f, 50.0f, "%.1f");
		ImGui::SetNextItemWidth(110.0f);
		ImGui::SliderFloat(u8("強さ"), &m_TerrainBrush.strength, 0.5f, 50.0f, "%.1f");

		if (ImGui::Button(u8("高さを保存")))
		{
			// 保存先が未設定なら Assets の下に作る
			if (terrain.dataPath.empty())
			{
				terrain.dataPath = "Assets/Terrain/Terrain_"
					+ std::to_string(ctx.selectedEntity) + ".r16";
			}
			if (Terrain::SaveHeights(terrain, terrain.dataPath))
			{
				ASSETDB->OnAssetAdded(terrain.dataPath);
				m_TerrainDirty = false;
			}
		}
		if (m_TerrainDirty)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), u8("未保存"));
		}
	}
	ImGui::EndGroup();

	if (!m_TerrainEditing) return false;

	// ---- マウス位置から光線を作る ---- //
	const ImVec2 mouse = ImGui::GetIO().MousePos;
	const float u = (mouse.x - imgPos.x) / imgSize.x;
	const float v = (mouse.y - imgPos.y) / imgSize.y;
	if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return true;   // 画面の外

	const XMMATRIX view = XMLoadFloat4x4(&cam.view);
	const XMMATRIX proj = XMLoadFloat4x4(&cam.proj);
	XMVECTOR det;
	const XMMATRIX invVP = XMMatrixInverse(&det, XMMatrixMultiply(view, proj));
	if (XMVectorGetX(XMVectorEqual(det, XMVectorZero()))) return true;

	// 画面の座標を -1〜1 に直して、手前と奥の2点を戻す
	const float ndcX = u * 2.0f - 1.0f;
	const float ndcY = 1.0f - v * 2.0f;
	const XMVECTOR nearP = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invVP);
	const XMVECTOR farP = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invVP);

	float3 origin{}, dir{};
	XMStoreFloat3(&origin, nearP);
	XMStoreFloat3(&dir, XMVector3Normalize(XMVectorSubtract(farP, nearP)));

	float3 hit{};
	if (!Terrain::Raycast(terrain, tr.world, origin, dir, cam.farZ, hit)) return true;

	// ---- ブラシの輪を描く ---- //
	{
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const XMMATRIX worldM = XMLoadFloat4x4(&tr.world);
		const XMMATRIX vp = XMMatrixMultiply(view, proj);

		ImVec2 pts[33];
		bool ok = true;
		for (int i = 0; i <= 32; ++i)
		{
			const float a = XM_2PI * i / 32.0f;
			const float px = hit.x + std::cos(a) * m_TerrainBrush.radius;
			const float pz = hit.z + std::sin(a) * m_TerrainBrush.radius;
			const float py = Terrain::SampleHeight(terrain, px, pz) + 0.05f;

			const XMVECTOR w = XMVector3TransformCoord(XMVectorSet(px, py, pz, 1.0f), worldM);
			const XMVECTOR c = XMVector3TransformCoord(w, vp);
			XMFLOAT3 sp; XMStoreFloat3(&sp, c);
			if (sp.z < 0.0f) { ok = false; break; }   // カメラの後ろ

			pts[i] = ImVec2(imgPos.x + (sp.x * 0.5f + 0.5f) * imgSize.x,
				imgPos.y + (0.5f - sp.y * 0.5f) * imgSize.y);
		}
		if (ok) dl->AddPolyline(pts, 33, IM_COL32(80, 220, 120, 220), 0, 2.0f);
	}

	// ---- 左ドラッグで彫る ---- //
	if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		// 平坦化は押し始めた地点の高さを目標にする
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			m_TerrainBrush.targetHeight = hit.y;

		Terrain::Brush brush = m_TerrainBrush;
		if (ImGui::GetIO().KeyShift && brush.mode == Terrain::BrushMode::Raise)
			brush.mode = Terrain::BrushMode::Lower;   // Shift で逆向き(Unity と同じ)

		Terrain::ApplyBrush(terrain, hit, brush, ImGui::GetIO().DeltaTime);
		Terrain::BuildMesh(world, ctx.selectedEntity);
		terrain.BuiltSettings = Terrain::HashSettings(terrain);   // 作り直しの二重起動を防ぐ
		m_TerrainDirty = true;
	}
	return true;   // 地形編集中はギズモに渡さない
}
