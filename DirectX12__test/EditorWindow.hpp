/*! ************************************************************
 * \file   EditorWindow.hpp
 * \brief  エディタのシェル(ドック配置とパネルの所有)
 *
 * 作成者 keeep
 * 作成日 2026/5/22
 * 更新履歴	5.22 作成
 *			5.23 Entityフィルターの作成
 *			5.28 Prototypeに向けてリファクタリング
 *			5.29 レイアウトの変更
 *			9.12 パネルを EditorPanel 派生のクラスへ分離。
 *			     ここに残るのはドック配置とパネルの束ねだけ
 *
 * \note パネルを増やすときは対応する EditorPanel を作り、
 *       コンストラクタの AddPanel に 1 行足す
 * *********************************************************************/
#pragma once

#include <memory>
#include <vector>

#include "SceneManager.hpp"
#include "World.hpp"
#include "DirectX.hpp"
#include "imguiinit.hpp"
#include "RenderTexture.hpp"

#include "EditorContext.hpp"
#include "EditorPanel.hpp"
#include "EditorMenuBar.hpp"
#include "ViewportPanel.hpp"
#include "UndoHistory.hpp"

class EditorWindow
{
public:
	EditorWindow(
		_In_ DirectXApp& app,
		_In_ SceneManager& sceneManager);

	/// @brief 描画処理
	/// @param sceneManager 描画に必要なシーンマネージャーの参照
	void Draw(_In_ SceneManager& sceneManager);

	inline RenderTexture* GetGameRenderTexture() const { return m_Viewport.GameTexture(); }

	inline RenderTexture* GetEditorRenderTexture() const { return m_Viewport.EditorTexture(); }

	ImVec2 GetViewportSize() const { return m_Context.viewportSize; }

	void ReleaseRenderTextures() { m_Viewport.ReleaseRenderTextures(); }

	/// @brief ゲーム画面が実際に表示されているか(タブ非選択・折りたたみで false)
	bool IsGameViewVisible() const noexcept { return m_Viewport.IsGameViewVisible(); }

	/// @brief エディタ画面が実際に表示されているか
	bool IsEditorViewVisible() const noexcept { return m_Viewport.IsEditorViewVisible(); }

private:
	/// @brief パネルを 1 枚追加して、その参照を返す
	template<class T>
	T& AddPanel()
	{
		auto p = std::make_unique<T>();
		T& ref = *p;
		m_Panels.push_back(std::move(p));
		return ref;
	}

	/// @brief 独立したウィンドウとして 1 枚描く
	void DrawWindowed(_In_ EditorPanel& panel);

	/// @brief 現在のワークスペースに合わせてドック配置を組み直す
	void BuildWorkspaceLayout(_In_ unsigned int dockspaceID, _In_ const ImVec2& size);

	/// @brief ビルド進捗のオーバーレイ
	void DrawBuildOverlay();

	EditorContext m_Context;
	EditorMenuBar m_MenuBar;
	ViewportPanel m_Viewport;
	UndoHistory m_History;
	Scene* m_HistoryScene = nullptr;	// m_History が追跡しているシーン
	bool m_HistoryEditing = false;

	std::vector<std::unique_ptr<EditorPanel>> m_Panels;

	// 「詳細パネル」のタブに入るパネル(所有は m_Panels)
	EditorPanel* m_AssetPanel = nullptr;
	EditorPanel* m_ScenePanel = nullptr;
	EditorPanel* m_MemoryPanel = nullptr;

	// 独立ウィンドウのパネル(所有は m_Panels)
	EditorPanel* m_HierarchyPanel = nullptr;
	EditorPanel* m_InspectorPanel = nullptr;
	EditorPanel* m_ConsolePanel = nullptr;
	EditorPanel* m_ProfilerPanel = nullptr;
	EditorPanel* m_MmdPlayerPanel = nullptr;
	EditorPanel* m_StylePanel = nullptr;

	bool m_ShowDetails = true;		// 下: 詳細パネル
	bool m_DockLayout = false;
	float m_BuildOverlayTimer = 0.0f;
};
