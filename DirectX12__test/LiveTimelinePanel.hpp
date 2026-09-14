/*!*************************************************************
 * ile   LiveTimelinePanel.hpp
 * rief  ライブ演出タイムラインのエディタパネル
 *
 * 作成者 keeep
 * 作成日 2026/9/14
 * 更新履歴	9.14 engine のパネル分割に合わせて EditorWindow から移植
 * *********************************************************************/
#pragma once

#include <string>

#include "EditorPanel.hpp"
#include "EditorContext.hpp"

class LiveTimelinePanel : public EditorPanel
{
public:
	const char* Title() const override
	{
		// u8() は一時 std::string の c_str() なので、返すと寿命が切れる。static に保持する
		static const std::string title = IMGUI::ToUTF8("ライブタイムライン");
		return title.c_str();
	}
	void Draw(_In_ EditorContext& ctx) override;
};
