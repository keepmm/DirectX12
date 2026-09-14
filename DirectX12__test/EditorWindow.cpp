/*! ************************************************************
 * \file   EditorWindow.cpp
 * \brief  エディタのシェル(ドック配置とパネルの所有)
 *
 * 作成者 keeep
 * 作成日 2026/5/22
 * 更新履歴	9.12 パネルを EditorPanel 派生のクラスへ分離
 * *********************************************************************/
#include "EditorWindow.hpp"

#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "BuildSystem.hpp"
#include "Input.hpp"

#include "HierarchyPanel.hpp"
#include "InspectorPanel.hpp"
#include "AssetBrowserPanel.hpp"
#include "ScenePanel.hpp"
#include "ConsolePanel.hpp"
#include "MemoryPanel.hpp"
#include "ProfilerPanel.hpp"
#include "StyleSettingPanel.hpp"
#include "MmdPlayerPanel.hpp"

EditorWindow::EditorWindow(DirectXApp& app, SceneManager& sceneManager)
	: m_Context(app, sceneManager)
{
	m_Viewport.Init();

	// ---- パネルの登録 ---- //
	m_HierarchyPanel = &AddPanel<HierarchyPanel>();
	m_InspectorPanel = &AddPanel<InspectorPanel>();
	m_ConsolePanel   = &AddPanel<ConsolePanel>();
	m_ProfilerPanel  = &AddPanel<ProfilerPanel>();
	m_MmdPlayerPanel = &AddPanel<MmdPlayerPanel>();
	m_AssetPanel     = &AddPanel<AssetBrowserPanel>();
	m_ScenePanel     = &AddPanel<ScenePanel>();
	m_MemoryPanel    = &AddPanel<MemoryPanel>();
	m_StylePanel     = &AddPanel<StyleSettingPanel>();

	m_StylePanel->visible = false;	// スタイル設定は既定で閉じておく

	// 「ウィンドウ」メニュー(登録順に並ぶ)。×で閉じたパネルはここから開き直す
	m_MenuBar.AddWindowToggle(m_HierarchyPanel->Title(), &m_HierarchyPanel->visible);
	m_MenuBar.AddWindowToggle(IMGUI::ToUTF8("ゲーム画面"),           &m_Viewport.showGameView);
	m_MenuBar.AddWindowToggle(IMGUI::ToUTF8("エディタ画面"),         &m_Viewport.showEditorView);
	m_MenuBar.AddWindowToggle(m_InspectorPanel->Title(), &m_InspectorPanel->visible);
	m_MenuBar.AddWindowToggle(IMGUI::ToUTF8("詳細パネル"),           &m_ShowDetails);
	m_MenuBar.AddWindowToggle(m_ConsolePanel->Title(),   &m_ConsolePanel->visible);
	m_MenuBar.AddWindowToggle(m_ProfilerPanel->Title(),  &m_ProfilerPanel->visible);
	m_MenuBar.AddWindowToggle(m_MmdPlayerPanel->Title(), &m_MmdPlayerPanel->visible);
	m_MenuBar.AddWindowToggle(m_StylePanel->Title(),     &m_StylePanel->visible);
}

void EditorWindow::DrawWindowed(EditorPanel& panel)
{
	// 閉じているパネルは Begin 自体を呼ばない(空のタブを残さない)
	if (!panel.visible) return;

	// &panel.visible を渡すと右上に×ボタンが出て、押すと false になる
	if (ImGui::Begin(panel.Title(), &panel.visible))
	{
		panel.Draw(m_Context);
	}
	ImGui::End();
}

void EditorWindow::DrawBuildOverlay()
{
	BuildSystem::Update();

	// ビルド中は右下に進捗オーバーレイを表示
	if (!BuildSystem::IsBuilding() &&
		!(BuildSystem::GetProgress() >= 1.0f && m_BuildOverlayTimer > 0.0f))
	{
		return;
	}

	if (BuildSystem::IsBuilding()) m_BuildOverlayTimer = 2.0f;	// 完了後2秒だけ残す
	else                           m_BuildOverlayTimer -= ImGui::GetIO().DeltaTime;

	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(
		ImVec2(vp->WorkPos.x + vp->WorkSize.x - 10.0f,
			vp->WorkPos.y + vp->WorkSize.y - 10.0f),
		ImGuiCond_Always, ImVec2(1.0f, 1.0f));	// 右下基準
	ImGui::SetNextWindowBgAlpha(0.85f);
	ImGui::Begin(u8("ビルド進捗"), nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking);

	const float p = BuildSystem::GetProgress();
	// MSBuild 区間(進捗が動かない)はバーを流れるアニメーションにする
	if (BuildSystem::IsBuilding() && p <= 0.05f)
		ImGui::ProgressBar(-1.0f * (float)ImGui::GetTime(), ImVec2(260, 0), u8("MSBuild..."));
	else
		ImGui::ProgressBar(p, ImVec2(260, 0));

	ImGui::TextUnformatted(BuildSystem::GetStage().c_str());
	ImGui::End();
}

void EditorWindow::Draw(SceneManager& sceneManager)
{
	DrawBuildOverlay();
	ImGuizmo::BeginFrame();

	// このフレームのアクティブシーン(切り替え中は一瞬 nullptr になる)
	m_Context.activeScene = sceneManager.GetActiveScene();

	// ---- メニューバー ---- //
	m_MenuBar.Draw(m_Context);
	if (m_MenuBar.relayoutRequested)
	{
		m_DockLayout = false;
		m_MenuBar.relayoutRequested = false;
	}

	// ---- 上部の再生ツールバー ---- //
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	const float h = ImGui::GetFrameHeight();
	if (ImGui::BeginViewportSideBar("##PlayToolbar", viewport, ImGuiDir_Up, h,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			m_MenuBar.DrawPlayControl(m_Context.activeScene);
			ImGui::EndMenuBar();
		}
	}
	ImGui::End();

	// ---- ドッキングスペース ---- //
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	const ImGuiWindowFlags dockWindowFlags = ImGuiWindowFlags_NoDocking |
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

	const ImGuiID dockspaceID = ImGui::GetID("EditorDockSpace");
	ImGui::DockSpace(dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

	// ドッキングレイアウトの初期設定(ワークスペース切替時にも組み直す)
	if (!m_DockLayout || ImGui::DockBuilderGetNode(dockspaceID) == nullptr)
	{
		BuildWorkspaceLayout(dockspaceID, viewport->WorkSize);
		m_DockLayout = true;
	}
	ImGui::End();

	// ---- 各パネル ---- //
	DrawWindowed(*m_HierarchyPanel);

	INPUT->SetViewportHovered(false);
	m_Viewport.DrawGameView(m_Context);
	m_Viewport.DrawEditorView(m_Context);

	DrawWindowed(*m_InspectorPanel);
	DrawWindowed(*m_ConsolePanel);
	DrawWindowed(*m_MmdPlayerPanel);
	DrawWindowed(*m_ProfilerPanel);

	// ---- 詳細パネル(タブで 3 枚を切り替える) ---- //
	if (m_ShowDetails)
	{
		if (ImGui::Begin(u8("詳細パネル"), &m_ShowDetails))
		{
			if (ImGui::BeginTabBar(u8("詳細パネルタブ")))
			{
				if (ImGui::BeginTabItem(m_AssetPanel->Title()))
				{
					m_AssetPanel->Draw(m_Context);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(m_ScenePanel->Title()))
				{
					if (m_Context.activeScene) m_ScenePanel->Draw(m_Context);
					ImGui::EndTabItem();
				}

				if (ImGui::BeginTabItem(m_MemoryPanel->Title()))
				{
					m_MemoryPanel->Draw(m_Context);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	// ---- スタイル設定(既定で閉じている単独ウィンドウ) ---- //
	DrawWindowed(*m_StylePanel);
}

void EditorWindow::BuildWorkspaceLayout(unsigned int dockspaceID, const ImVec2& size)
{
	ImGui::DockBuilderRemoveNode(dockspaceID);
	ImGui::DockBuilderAddNode(dockspaceID, ImGuiDockNodeFlags_None);
	ImGui::DockBuilderSetNodeSize(dockspaceID, size);

	ImGuiID dockMainID = dockspaceID;

	if (m_MenuBar.workspace == EditorMenuBar::Workspace::Lighting)
	{
		// ライト編集: 下半分をタイムラインに使い、画面は上に大きく取る
		ImGuiID dockLeftID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Left, 0.14f, nullptr, &dockMainID);
		ImGuiID dockRightID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Right, 0.20f, nullptr, &dockMainID);
		ImGuiID dockBottomID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Down, 0.42f, nullptr, &dockMainID);

		ImGui::DockBuilderDockWindow(u8("アウトライナー"), dockLeftID);
		ImGui::DockBuilderDockWindow(u8("ゲーム画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("エディタ画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("プロパティパネル"), dockRightID);
		ImGui::DockBuilderDockWindow(u8("ライブタイムライン"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("MMDコントローラー"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("コンソール"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("詳細パネル"), dockBottomID);
	}
	else
	{
		// ゲームエンジン: 従来のレイアウト
		ImGuiID dockLeftID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Left, 0.12f, nullptr, &dockMainID);
		ImGuiID dockRightID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Right, 0.15f, nullptr, &dockMainID);
		ImGuiID dockBottomID = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Down, 0.20f, nullptr, &dockMainID);

		ImGui::DockBuilderDockWindow(u8("アウトライナー"), dockLeftID);
		ImGui::DockBuilderDockWindow(u8("ゲーム画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("エディタ画面"), dockMainID);
		ImGui::DockBuilderDockWindow(u8("プロパティパネル"), dockRightID);
		ImGui::DockBuilderDockWindow(u8("詳細パネル"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("コンソール"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("ライブタイムライン"), dockBottomID);
		ImGui::DockBuilderDockWindow(u8("MMDコントローラー"), dockBottomID);
	}

	ImGui::DockBuilderFinish(dockspaceID);
}
