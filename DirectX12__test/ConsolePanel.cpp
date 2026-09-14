/*!*************************************************************
 * \file   ConsolePanel.cpp
 * \brief  ログの表示
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "ConsolePanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"


void ConsolePanel::Draw(EditorContext&)
{
	if (ImGui::Button(u8("クリア"))) { /* 後述: Logger側にClear追加 */ }
	ImGui::SameLine();
	static bool autoScroll = true;
	ImGui::Checkbox(u8("自動スクロール"), &autoScroll);

	ImGui::Separator();
	ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false,
		ImGuiWindowFlags_HorizontalScrollbar);

	for (const auto& line : LOG->GetRecentLogs())
	{
		std::string utf8 = IMGUI::ToUTF8(line);

		ImVec4 color(1, 1, 1, 1);
		if (line.find("[ERROR]") != std::string::npos) color = ImVec4(1.0f, 0.3f, 0.3f, 1);
		else if (line.find("[WARNING]") != std::string::npos) color = ImVec4(1.0f, 0.85f, 0.3f, 1);
		else if (line.find("[DEBUG]") != std::string::npos) color = ImVec4(0.5f, 0.7f, 1.0f, 1);

		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s", utf8.c_str());
		ImGui::PopStyleColor();
	}

	if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
}
