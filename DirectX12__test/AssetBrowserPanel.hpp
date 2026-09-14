/*!*************************************************************
 * \file   AssetBrowserPanel.hpp
 * \brief  アセットブラウザ(Assets フォルダの一覧と操作)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorPanel 派生のクラスとして作成
 * *********************************************************************/
#pragma once

#include <string>

#include "EditorPanel.hpp"
#include "EditorContext.hpp"

class AssetBrowserPanel : public EditorPanel
{
public:
	const char* Title() const override
	{
		// u8() は一時 std::string の c_str() なので、返すと寿命が切れる。static に保持する
		static const std::string title = IMGUI::ToUTF8("アセット");
		return title.c_str();
	}
	void Draw(_In_ EditorContext& ctx) override;

private:
	float m_AssetCellSize = 72.0f;

	// スクリプト新規作成
	bool m_ShowCreateScriptPopup = false;
	char m_NewScriptName[64] = "NewScript";

	// リネーム / 削除の確認ダイアログ
	bool m_ShowRenamePopup = false;
	bool m_ShowDeletePopup = false;
	char m_RenameBuffer[128] = "";
	std::string m_ContextTarget;   // 右クリックされたアイテムのフルパス(空なら空白部分)
};
