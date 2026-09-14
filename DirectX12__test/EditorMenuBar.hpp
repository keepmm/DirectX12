/*!*************************************************************
 * \file   EditorMenuBar.hpp
 * \brief  メニューバー / 再生コントロール / プロジェクト切り替え
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#pragma once

#include <array>
#include <vector>
#include <string>

#include "EditorContext.hpp"

class EditorMenuBar
{
public:
	/// @brief メインメニューバーとプロジェクト切り替えダイアログを描く
	void Draw(_In_ EditorContext& ctx);

	/// @brief 再生 / 一時停止ボタン(上部ツールバーの中に置かれる)
	void DrawPlayControl(_In_ Scene* activeScene);

	// ---- 「ウィンドウ」メニュー ---- //
	struct WindowToggle
	{
		std::string label;	// メニューに出す名前(UTF-8)。u8() のポインタは寿命が短いのでコピーして持つ
		bool* visible;		// パネル側の表示フラグ
	};

	/// @brief EditorWindow がパネルを登録する(登録順にメニューへ並ぶ)
	void AddWindowToggle(_In_ const std::string& label, _In_ bool* visible)
	{
		m_WindowToggles.push_back({ label, visible });
	}

	/// @brief ワークスペースが切り替わったら true(EditorWindow が読んで消す)
	bool relayoutRequested = false;

	/// @brief 用途ごとのパネル配置
	enum class Workspace
	{
		Engine,		// ゲームエンジン用(従来のレイアウト)
		Lighting,	// ライト/演出編集用(下半分がタイムライン)
	};
	Workspace workspace = Workspace::Engine;

private:
	void DrawProjectDialog();

	/// @brief メニューバー右端の「最小化」「閉じる」
	/// @note メインウィンドウはボーダレス(WS_POPUP)で OS のタイトルバーが無いので自前で描く
	void DrawWindowButtons();

	std::vector<WindowToggle> m_WindowToggles;

	std::string m_PlaySnap;

	// ビルド設定
	std::array<char, 256> m_BuildOutputDir = { "Build" };
	std::array<char, 128> m_BuildGameName = { "MyGame" };
	// 空ならプロジェクトの開始シーンを引き継ぐ(ビルド設定を開いたときに埋まる)
	std::array<char, 256> m_BuildStartScene{};
	bool m_BuildUsedAssetsOnly = true;	// ビルド時に未使用アセットを出力しない

	// プロジェクト切り替え(ランチャーを開いて終了するかの確認)
	bool m_ShowSwitchProject = false;
	std::string m_ProjectError;
};
