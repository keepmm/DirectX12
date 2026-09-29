/*!*************************************************************
 * \file   ViewportPanel.hpp
 * \brief  ゲーム画面 / エディタ画面のビューポート
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 *
 * \note 2 枚のウィンドウを描くので EditorPanel は継承していない。
 *       レンダーテクスチャの所有もここ(EditorWindow が外へ転送する)
 * *********************************************************************/
#pragma once

#include <memory>

#include "EditorContext.hpp"
#include "RenderTexture.hpp"
#include "Terrain.hpp"

struct CameraComponent;

class ViewportPanel
{
public:
	/// @brief 1280x720 のレンダーテクスチャを 2 枚用意する
	void Init();

	/// @brief ゲーム画面(レンダーテクスチャ + シーンフェード + モデルのドロップ)
	void DrawGameView(_In_ EditorContext& ctx);

	/// @brief エディタ画面(レンダーテクスチャ + ImGuizmo)
	void DrawEditorView(_In_ EditorContext& ctx);

	void ReleaseRenderTextures();

	RenderTexture* GameTexture() const noexcept { return m_GameRenderTexture.get(); }
	RenderTexture* EditorTexture() const noexcept { return m_EditorRenderTexture.get(); }

	/// @brief ゲーム画面が実際に表示されているか(タブ非選択・折りたたみで false)
	bool IsGameViewVisible() const noexcept { return m_GameViewVisible; }

	/// @brief エディタ画面が実際に表示されているか
	bool IsEditorViewVisible() const noexcept { return m_EditorViewVisible; }

	/// @brief 「ウィンドウ」メニュー / ×ボタンで切り替える表示フラグ
	bool showGameView = true;
	bool showEditorView = true;

private:
	// ゲーム画面用のレンダーテクスチャ
	std::unique_ptr<RenderTexture> m_GameRenderTexture;
	bool m_GameTextureHandleValid = false;

	// エディタ用のレンダーテクスチャ
	std::unique_ptr<RenderTexture> m_EditorRenderTexture;
	bool m_EditorTextureHandleValid = false;

	// タブで隠れているビューポートは描画を省くための可視フラグ
	bool m_GameViewVisible = true;
	bool m_EditorViewVisible = true;

	int m_GizmoOperation = 7;

	// ---- 地形ブラシ ---- //
	bool m_TerrainEditing = false;
	Terrain::Brush m_TerrainBrush{};
	bool m_TerrainDirty = false;	// 彫ったまま保存していないか

	/// @brief エディタ画面での地形ブラシ。ギズモより先に処理する
	/// @return ブラシが入力を使ったか(true ならギズモを動かさない)
	bool DrawTerrainBrush(_In_ EditorContext& ctx, _In_ const CameraComponent& cam,
		_In_ const ImVec2& imgPos, _In_ const ImVec2& imgSize);

	int m_AspectMode = 1;
	int m_CustomWidth = 1280;
	int m_CustomHeight = 720;
};
