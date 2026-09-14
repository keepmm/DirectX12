/*!*************************************************************
 * \file   StyleSettingPanel.cpp
 * \brief  GUI のスタイル設定
 *
 * 作成者 keeep
 * 作成日 2026/9/12
 * 更新履歴	9.12 EditorWindow から分離して作成
 * *********************************************************************/
#include "StyleSettingPanel.hpp"

#include "imguiinit.hpp"
#include "imgui_internal.h"
#include "Logger.hpp"
#include "Util.hpp"
#include "Theme.hpp"


void StyleSettingPanel::Draw(EditorContext&)
{
	//---- スタイル設定 ---- //
		// ---- テーマ ---- //
		static int themeIdx = 0;
		static ImVec4 accent = ImVec4(0.26f, 0.59f, 0.98f, 1.0f);
		const char* themes[] = { "Dark", "Light", "Classic" };
		if (ImGui::Combo(u8("テーマ"), &themeIdx, themes, IM_ARRAYSIZE(themes)))
			ApplyTheme((uiTheme)themeIdx, accent);
		if (ImGui::ColorEdit3(u8("アクセント色"), &accent.x))
			ApplyTheme((uiTheme)themeIdx, accent);

		ImGui::Separator();

		// ---- フォント切替 ----
		ImGuiIO& io = ImGui::GetIO();
		if (ImGui::BeginCombo(u8("フォント"),
			io.FontDefault ? "current" : "default"))
		{
			for (int i = 0; i < io.Fonts->Fonts.Size; ++i)
			{
				ImFont* f = io.Fonts->Fonts[i];
				ImGui::PushID(i);
				bool sel = (io.FontDefault == f);
				// フォント名は登録時のデバッグ名が入る
				if (ImGui::Selectable(u8(f->GetDebugName()), sel))
					io.FontDefault = f;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}
