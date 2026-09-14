/*!*************************************************************
 * \file   MemoryPanel.hpp
 * \brief  メモリ使用量の表示
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#pragma once

#include "EditorPanel.hpp"
#include "EditorContext.hpp"

class MemoryPanel : public EditorPanel
{
public:
	const char* Title() const override
	{
		// u8() は一時 std::string の c_str() なので、返すと寿命が切れる。static に保持する
		static const std::string title = IMGUI::ToUTF8("メモリ消費量");
		return title.c_str();
	}
	void Draw(_In_ EditorContext& ctx) override;
};
