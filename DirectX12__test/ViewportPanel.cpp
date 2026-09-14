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
		const ImVec2 availableSize = ImGui::GetContentRegionAvail();
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
			ctx.app.WaitForGPUIdle();
			m_GameRenderTexture->Init(newWidth, newHeight);
		}

		ctx.viewportPos = ImGui::GetCursorScreenPos();
		ctx.viewportSize = availableSize;

		// レンダーテクスチャが有効な場合は、ImGuiに描画
		if (m_GameRenderTexture && m_GameTextureHandleValid)
		{
			ImGui::Image(static_cast<ImTextureID>(m_GameRenderTexture->GetSRV().ptr),
				availableSize, ImVec2(0, 0), ImVec2(1, 1));

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

					ctx.selectedEntity = EntityFactory::SpawnModelFromFile(
						ctx.activeScene->GetWorld(), modelpath, fragPosition, ctx.activeScene);
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

						if (ImGuizmo::Manipulate(
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
