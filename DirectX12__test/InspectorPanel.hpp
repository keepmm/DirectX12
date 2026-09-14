/*!*************************************************************
 * \file   InspectorPanel.hpp
 * \brief  インスペクタ(選択エンティティのコンポーネント編集)
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorPanel 派生のクラスとして作成
 * *********************************************************************/
#pragma once

#include "EditorPanel.hpp"
#include "EditorContext.hpp"

class InspectorPanel : public EditorPanel
{
public:
	const char* Title() const override
	{
		// u8() は一時 std::string の c_str() なので、返すと寿命が切れる。static に保持する
		static const std::string title = IMGUI::ToUTF8("プロパティパネル");
		return title.c_str();
	}
	void Draw(_In_ EditorContext& ctx) override;

private:
	/// @brief インスペクターの描画
	void DrawInspector(_In_ EditorContext& ctx, _In_ World& world, _In_ Scene* scene);

	/// @brief InspectorにAddComponentのポップアップを表示する
	void DrawAddComponentPopup(_In_ World& world, _In_ Entity entity);

	char m_ScriptSerachBuffer[128] = {};
	bool m_FocusScriptSearch = false;

	char m_AddCompSearchBuffer[128] = {};
	bool m_FocusAddCompSearch = false;
};
